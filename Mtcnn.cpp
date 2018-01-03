#include "Mtcnn.h"
#include "net.h"
#include <cmath>
#include <iostream>

using namespace std;

const float m_nmsThreshold[3] = { 0.5f, 0.7f, 0.7f };
const float m_threshold[3] = { 0.6f, 0.6f, 0.6f };
const float m_mean_vals[3] = { 127.5, 127.5, 127.5 };
const float m_norm_vals[3] = { 0.0078125, 0.0078125, 0.0078125 };

bool cmpScore(SOrderScore lsh, SOrderScore rsh)
{
    if (lsh.score < rsh.score)
        return true;
    else
        return false;
}

int GetNcnnImageConvertType(imageType type)
{
    switch (type)
    {
    case eBGR:
    default:
        return ncnn::Mat::PIXEL_BGR2RGB;

    }
}

CMtcnn::CMtcnn()
    : m_ImgType(eBGR)
    , m_ImgWidth(0)
    , m_ImgHeight(0)
{
    // [TODO] - Refine naming and refactor code
    // Re-implement from the following link
    // https://github.com/kpzhang93/MTCNN_face_detection_alignment/tree/master/code/codes/MTCNNv1
}


void CMtcnn::LoadModel(const char* pNetStructPath, const char* pNetWeightPath, const char* rNetStructPath, const char* rNetWeightPath, const char* oNetStructPath, const char* oNetWeightPath)
{
    m_Pnet.load_param(pNetStructPath);
    m_Pnet.load_model(pNetWeightPath);
    m_Rnet.load_param(rNetStructPath);
    m_Rnet.load_model(rNetWeightPath);
    m_Onet.load_param(oNetStructPath);
    m_Onet.load_model(oNetWeightPath);
}

void CMtcnn::SetParam(unsigned int width, unsigned int height, imageType type /*= eBGR*/, int iMinSize /*= 90*/, float fPyramidFactor /*= 0.709*/)
{
    m_ImgWidth = width;
    m_ImgHeight = height;
    m_ImgType = type;

    m_pyramidScale = GetPyramidScale(width, height, iMinSize, fPyramidFactor);
}

std::vector<float> CMtcnn::GetPyramidScale(unsigned int width, unsigned int height, int iMinSize, float fPyramidFactor)
{
    vector<float> retScale;
    float minl = width < height ? width : height;
    float MIN_DET_SIZE = 12;

    float m = MIN_DET_SIZE / iMinSize;
    minl = minl * m;

    while (minl > MIN_DET_SIZE)
    {
        if (!retScale.empty())
        {
            m = m * fPyramidFactor;
        }

        retScale.push_back(m);
        minl = minl * fPyramidFactor;
    }

    return std::move(retScale);
}
void CMtcnn::GenerateBbox(ncnn::Mat score, ncnn::Mat location, std::vector<SBoundingBox>& boundingBox_, std::vector<SOrderScore>& bboxScore_, float scale)
{
    int stride = 2;
    int cellsize = 12;
    int count = 0;
    //score p
    float *p = score.channel(1);//score.data + score.cstep;
    float *plocal = location.data;
    SBoundingBox bbox;
    SOrderScore order;
    for (int row = 0; row<score.h; row++)
    {
        for (int col = 0; col<score.w; col++)
        {
            if (*p>m_threshold[0])
            {
                bbox.score = *p;
                order.score = *p;
                order.oriOrder = count++;
                bbox.x1 = round((stride*col + 1) / scale);
                bbox.y1 = round((stride*row + 1) / scale);
                bbox.x2 = round((stride*col + 1 + cellsize) / scale);
                bbox.y2 = round((stride*row + 1 + cellsize) / scale);
                bbox.bExist = true;
                bbox.area = (bbox.x2 - bbox.x1)*(bbox.y2 - bbox.y1);
                for (int channel = 0; channel<4; channel++)
                    bbox.regreCoord[channel] = location.channel(channel)[0];
                boundingBox_.push_back(bbox);
                bboxScore_.push_back(order);
            }
            ++p;
            ++plocal;
        }
    }
}
void CMtcnn::Nms(std::vector<SBoundingBox> &boundingBox_, std::vector<SOrderScore> &bboxScore_, const float overlap_threshold, string modelname)
{
    if (boundingBox_.empty())
    {
        return;
    }
    std::vector<int> heros;
    //sort the score
    sort(bboxScore_.begin(), bboxScore_.end(), cmpScore);

    int order = 0;
    float IOU = 0;
    float maxX = 0;
    float maxY = 0;
    float minX = 0;
    float minY = 0;
    while (bboxScore_.size()>0)
    {
        order = bboxScore_.back().oriOrder;
        bboxScore_.pop_back();
        if (order<0)continue;
        if (boundingBox_.at(order).bExist == false) continue;
        heros.push_back(order);
        boundingBox_.at(order).bExist = false;//delete it

        for (int num = 0; num<boundingBox_.size(); num++)
        {
            if (boundingBox_.at(num).bExist)
            {
                //the iou
                maxX = (boundingBox_.at(num).x1>boundingBox_.at(order).x1) ? boundingBox_.at(num).x1 : boundingBox_.at(order).x1;
                maxY = (boundingBox_.at(num).y1>boundingBox_.at(order).y1) ? boundingBox_.at(num).y1 : boundingBox_.at(order).y1;
                minX = (boundingBox_.at(num).x2<boundingBox_.at(order).x2) ? boundingBox_.at(num).x2 : boundingBox_.at(order).x2;
                minY = (boundingBox_.at(num).y2<boundingBox_.at(order).y2) ? boundingBox_.at(num).y2 : boundingBox_.at(order).y2;
                //maxX1 and maxY1 reuse 
                maxX = ((minX - maxX + 1)>0) ? (minX - maxX + 1) : 0;
                maxY = ((minY - maxY + 1)>0) ? (minY - maxY + 1) : 0;
                //IOU reuse for the area of two bbox
                IOU = maxX * maxY;
                if (!modelname.compare("Union"))
                    IOU = IOU / (boundingBox_.at(num).area + boundingBox_.at(order).area - IOU);
                else if (!modelname.compare("Min"))
                {
                    IOU = IOU / ((boundingBox_.at(num).area<boundingBox_.at(order).area) ? boundingBox_.at(num).area : boundingBox_.at(order).area);
                }
                if (IOU>overlap_threshold)
                {
                    boundingBox_.at(num).bExist = false;
                    for (vector<SOrderScore>::iterator it = bboxScore_.begin(); it != bboxScore_.end(); it++)
                    {
                        if ((*it).oriOrder == num)
                        {
                            (*it).oriOrder = -1;
                            break;
                        }
                    }
                }
            }
        }
    }
    for (int i = 0; i<heros.size(); i++)
        boundingBox_.at(heros.at(i)).bExist = true;
}
void CMtcnn::RefineAndSquareBbox(vector<SBoundingBox> &vecBbox, const int &height, const int &width)
{
    if (vecBbox.empty())
    {
        //cout << "Bbox is empty!!" << endl;
        return;
    }
    float bbw = 0, bbh = 0, maxSide = 0;
    float h = 0, w = 0;
    float x1 = 0, y1 = 0, x2 = 0, y2 = 0;
    for (vector<SBoundingBox>::iterator it = vecBbox.begin(); it != vecBbox.end(); it++)
    {
        if ((*it).bExist)
        {
            bbw = (*it).x2 - (*it).x1 + 1;
            bbh = (*it).y2 - (*it).y1 + 1;
            x1 = (*it).x1 + (*it).regreCoord[0] * bbw;
            y1 = (*it).y1 + (*it).regreCoord[1] * bbh;
            x2 = (*it).x2 + (*it).regreCoord[2] * bbw;
            y2 = (*it).y2 + (*it).regreCoord[3] * bbh;

            w = x2 - x1 + 1;
            h = y2 - y1 + 1;

            maxSide = (h>w) ? h : w;
            x1 = x1 + w*0.5 - maxSide*0.5;
            y1 = y1 + h*0.5 - maxSide*0.5;
            (*it).x2 = round(x1 + maxSide - 1);
            (*it).y2 = round(y1 + maxSide - 1);
            (*it).x1 = round(x1);
            (*it).y1 = round(y1);

            //boundary check
            if ((*it).x1<0)(*it).x1 = 0;
            if ((*it).y1<0)(*it).y1 = 0;
            if ((*it).x2>width)(*it).x2 = width - 1;
            if ((*it).y2>height)(*it).y2 = height - 1;

            it->area = (it->x2 - it->x1)*(it->y2 - it->y1);
        }
    }
}

void CMtcnn::Detect(const unsigned char* img, std::vector<SBoundingBox>& result)
{
    std::vector<SBoundingBox> firstBbox;
    std::vector<SBoundingBox> secondBbox;
    std::vector<SBoundingBox> thirdBbox;
    std::vector<SOrderScore> firstOrderScore;
    std::vector<SOrderScore> secondBboxScore;
    std::vector<SOrderScore> thirdBboxScore;

    ncnn::Mat ncnnImg = ncnn::Mat::from_pixels(img, GetNcnnImageConvertType(eBGR), m_ImgWidth, m_ImgHeight);
    ncnnImg.substract_mean_normalize(m_mean_vals, m_norm_vals);

    SOrderScore order;

    //First stage
    for (size_t i = 0; i < m_pyramidScale.size(); ++i)
    {
        ncnn::Mat score;
        ncnn::Mat location;
        std::vector<SBoundingBox> boundingBox;
        std::vector<SOrderScore> bboxScore;

        int hs = (int)ceil(m_ImgHeight * m_pyramidScale[i]);
        int ws = (int)ceil(m_ImgWidth * m_pyramidScale[i]);
        ncnn::Mat pyramidImg;
        resize_bilinear(ncnnImg, pyramidImg, ws, hs);
        ncnn::Extractor ex = m_Pnet.create_extractor();
        ex.set_light_mode(true);
        // [TODO] - Check if need to set_num_threads
        ex.input("data", pyramidImg);
        ex.extract("prob1", score);
        ex.extract("conv4-2", location);
        GenerateBbox(score, location, boundingBox, bboxScore, m_pyramidScale[i]);
        Nms(boundingBox, bboxScore, m_nmsThreshold[0]);

        for (vector<SBoundingBox>::iterator it = boundingBox.begin(); it != boundingBox.end(); it++)
        {
            if ((*it).bExist)
            {
                firstBbox.push_back(*it);
                order.score = (*it).score;
                order.oriOrder = firstOrderScore.size();
                firstOrderScore.push_back(order);
            }
        }
    }

    if (firstOrderScore.empty())
        return;

    Nms(firstBbox, firstOrderScore, m_nmsThreshold[0]);
    RefineAndSquareBbox(firstBbox, m_ImgHeight, m_ImgWidth);

    //second stage
    for (vector<SBoundingBox>::iterator it = firstBbox.begin(); it != firstBbox.end(); it++)
    {
        if ((*it).bExist)
        {
            ncnn::Mat tempImg;
            ncnn::Mat ncnnImg24;
            ncnn::Mat score;
            ncnn::Mat bbox;

            copy_cut_border(ncnnImg, tempImg, (*it).y1, m_ImgHeight - (*it).y2, (*it).x1, m_ImgWidth - (*it).x2);
            resize_bilinear(tempImg, ncnnImg24, 24, 24);
            ncnn::Extractor ex = m_Rnet.create_extractor();
            ex.set_light_mode(true);
            // [TODO] - Check if need to set_num_threads
            ex.input("data", ncnnImg24);
            ex.extract("prob1", score);
            ex.extract("conv5-2", bbox);

            if (*(score.data + score.cstep)>m_threshold[1])
            {
                for (int channel = 0; channel<4; channel++)
                    it->regreCoord[channel] = bbox.channel(channel)[0];//*(bbox.data+channel*bbox.cstep);

                it->area = (it->x2 - it->x1)*(it->y2 - it->y1);
                it->score = score.channel(1)[0];//*(score.data+score.cstep);
                secondBbox.push_back(*it);
                order.score = it->score;
                order.oriOrder = secondBboxScore.size();
                secondBboxScore.push_back(order);
            }
            else
            {
                (*it).bExist = false;
            }
        }
    }

    if (secondBboxScore.empty())
        return;

    Nms(secondBbox, secondBboxScore, m_nmsThreshold[1]);
    RefineAndSquareBbox(secondBbox, m_ImgHeight, m_ImgWidth);

    //third stage
    for (vector<SBoundingBox>::iterator it = secondBbox.begin(); it != secondBbox.end(); it++)
    {
        if ((*it).bExist)
        {
            ncnn::Mat tempImg;
            ncnn::Mat ncnnImg48;
            ncnn::Mat score;
            ncnn::Mat bbox;
            ncnn::Mat keyPoint;

            copy_cut_border(ncnnImg, tempImg, (*it).y1, m_ImgHeight - (*it).y2, (*it).x1, m_ImgWidth - (*it).x2);
            resize_bilinear(tempImg, ncnnImg48, 48, 48);
            ncnn::Extractor ex = m_Onet.create_extractor();
            ex.set_light_mode(true);
            ex.input("data", ncnnImg48);
            ex.extract("prob1", score);
            ex.extract("conv6-2", bbox);
            ex.extract("conv6-3", keyPoint);
            if (score.channel(1)[0] > m_threshold[2])
            {
                for (int channel = 0; channel < 4; channel++)
                    it->regreCoord[channel] = bbox.channel(channel)[0];
                it->area = (it->x2 - it->x1)*(it->y2 - it->y1);
                it->score = score.channel(1)[0];
                for (int num = 0; num < 5; num++)
                {
                    (it->ppoint)[num] = it->x1 + (it->x2 - it->x1)*keyPoint.channel(num)[0];
                    (it->ppoint)[num + 5] = it->y1 + (it->y2 - it->y1)*keyPoint.channel(num + 5)[0];
                }

                thirdBbox.push_back(*it);
                order.score = it->score;
                order.oriOrder = thirdBboxScore.size();
                thirdBboxScore.push_back(order);
            }
            else
                (*it).bExist = false;
        }
    }

    if (thirdBboxScore.empty())
        return;

    RefineAndSquareBbox(thirdBbox, m_ImgHeight, m_ImgWidth);
    Nms(thirdBbox, thirdBboxScore, m_nmsThreshold[2], "Min");
    result = thirdBbox;
}
