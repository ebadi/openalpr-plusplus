/*
 * Copyright (c) 2015 OpenALPR Technology, Inc.
 * Open source Automated License Plate Recognition [http://www.openalpr.com]
 *
 * This file is part of OpenALPR.
 *
 * OpenALPR is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License
 * version 3 as published by the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#include "tesseract_ocr.h"
#include "config.h"

#include "segmentation/charactersegmenter.h"

using namespace std;
using namespace cv;
using namespace tesseract;

namespace alpr
{

  TesseractOcr::TesseractOcr(Config* config)
  : OCR(config)
  {
    const string MINIMUM_TESSERACT_VERSION = "3.03";

    this->postProcessor.setConfidenceThreshold(config->postProcessMinConfidence, config->postProcessConfidenceSkipLevel);

    if (cmpVersion(tesseract.Version(), MINIMUM_TESSERACT_VERSION.c_str()) < 0)
    {
      std::cerr << "Warning: You are running an unsupported version of Tesseract." << endl;
      std::cerr << "Expecting at least " << MINIMUM_TESSERACT_VERSION << ", your version is: " << tesseract.Version() << endl;
    }

    string TessdataPrefix = config->getTessdataPrefix();
    if (cmpVersion(tesseract.Version(), "4.0.0") >= 0)
      TessdataPrefix += "tessdata/";

    // Tesseract requires the prefix directory to be set as an env variable
    tesseract.Init(TessdataPrefix.c_str(), config->ocrLanguage.c_str() 	);
    tesseract.SetVariable("save_blob_choices", "T");
    tesseract.SetVariable("debug_file", "/dev/null");
    tesseract.SetPageSegMode(PSM_SINGLE_CHAR);
  }

  TesseractOcr::~TesseractOcr()
  {
    tesseract.End();
  }

  std::vector<OcrChar> TesseractOcr::recognize_line(int line_idx, PipelineData* pipeline_data) {

    const int SPACE_CHAR_CODE = 32;

    std::vector<OcrChar> recognized_chars;
    if (this->config->debugOcr)
    {
      printf("\nDEBUG2_JSON:{"); //7
      
      printf("\nDEBUG2_JSON: \"p0x\": %d, \"p0y\":%d, \"p1x\":%d, \"p1y\": %d, \"p2x\": %d, \"p2y\": %d, \"p3x\": %d, \"p3y\": %d, ",
        pipeline_data->plate_corners[0].x,
        pipeline_data->plate_corners[0].y,
        pipeline_data->plate_corners[1].x,
        pipeline_data->plate_corners[1].y,
        pipeline_data->plate_corners[2].x,
        pipeline_data->plate_corners[2].y,
        pipeline_data->plate_corners[3].x,
        pipeline_data->plate_corners[3].y
      );
      
      printf("\nDEBUG2_JSON: \"rx\": %d,  \"ry\": %d, \"rw\": %d, \"rh\":%d, \"thresholds\": ",
        pipeline_data->regionOfInterest.x,
        pipeline_data->regionOfInterest.y,
        pipeline_data->regionOfInterest.width,
        pipeline_data->regionOfInterest.height
      );
      printf("\nDEBUG2_JSON:["); //7
    }


    int mc4 = 0;
    for (unsigned int i = 0; i < pipeline_data->thresholds.size(); i++)
    {
      mc4 ++;
      if (this->config->debugOcr)
      {
        if (mc4 > 1) { printf("\nDEBUG2_JSON:  ,\n") ;}
      }
      // Make it black text on white background
      bitwise_not(pipeline_data->thresholds[i], pipeline_data->thresholds[i]);
      tesseract.SetImage((uchar*) pipeline_data->thresholds[i].data,
                          pipeline_data->thresholds[i].size().width, pipeline_data->thresholds[i].size().height,
                          pipeline_data->thresholds[i].channels(), pipeline_data->thresholds[i].step1());

      if (this->config->debugOcr)
      {
        printf("\nDEBUG2_JSON:  {\"threshold\": %d, \"regions\":\n", i); //6
        printf("\nDEBUG2_JSON:    [\n"); //5
      }
      int absolute_charpos = 0;
      int mc3 = 0 ;
      for (unsigned int j = 0; j < pipeline_data->charRegions[line_idx].size(); j++)
      {
        mc3 ++;

        Rect expandedRegion = expandRect( pipeline_data->charRegions[line_idx][j], 2, 2, pipeline_data->thresholds[i].cols, pipeline_data->thresholds[i].rows) ;
        tesseract.SetRectangle(expandedRegion.x, expandedRegion.y, expandedRegion.width, expandedRegion.height);
        tesseract.Recognize(NULL);

        if (this->config->debugOcr)
        {
          if (mc3 > 1) { printf("\nDEBUG2_JSON:      ,\n") ;}
          printf("\nDEBUG2_JSON:      {\"region\": %d, \"x\": %d, \"y\": %d, \"height\": %d, \"width\": %d,  \"x1\": %d, \"y1\": %d, \"height1\": %d, \"width1\": %d,   \"ocr_detections\":\n", j,
              pipeline_data->charRegions[line_idx][j].x,
              pipeline_data->charRegions[line_idx][j].y,
              pipeline_data->charRegions[line_idx][j].height,
              pipeline_data->charRegions[line_idx][j].width,
              expandedRegion.x,
              expandedRegion.y,
              expandedRegion.height,
              expandedRegion.width); //6
          printf("\nDEBUG2_JSON:        [\n"); //8
        }

        tesseract::ResultIterator* ri = tesseract.GetIterator();
        tesseract::PageIteratorLevel level = tesseract::RIL_SYMBOL;
        int mc2 = 0 ;
        do
        {
          mc2 ++ ;
          if (this->config->debugOcr)
          {
            if (mc2 > 1) {printf("\nDEBUG2_JSON: ,\n") ;}
          }

          if (ri->Empty(level)) continue;

          const char* symbol = ri->GetUTF8Text(level);
          float conf = ri->Confidence(level);

          bool dontcare;
          int fontindex = 0;
          int pointsize = 0;

          const char* fontName = ri->WordFontAttributes(&dontcare, &dontcare, &dontcare, &dontcare, &dontcare, &dontcare, &pointsize, &fontindex);

          // Ignore NULL pointers, spaces, and characters that are way too small to be valid
          if(symbol != 0 && symbol[0] != SPACE_CHAR_CODE && pointsize >= config->ocrMinFontSize)
          {
            OcrChar c;
            c.char_index = absolute_charpos;
            c.confidence = conf;
            c.letter = string(symbol);
            recognized_chars.push_back(c);

            if (this->config->debugOcr)
            {
              printf("charpos%d line%d: threshold %d:  symbol %s, conf: %f font: %s (index %d) size %dpx", absolute_charpos, line_idx, i, symbol, conf, fontName, fontindex, pointsize);
              printf("\nDEBUG2_JSON:          {\"font_info\" : {\"fontName\":\"%s\", \"fontindex\":%d, \"pointsize\":%d, \"symbols\":\n", fontName, fontindex, pointsize); //2
              printf("\nDEBUG2_JSON:            [\n"); //1
            }
            bool indent = false;
            tesseract::ChoiceIterator ci(*ri);
            int mc1 = 0;
            do
            {
              mc1++ ;
              if (this->config->debugOcr)
              {
                if (mc1 > 1) {printf("\nDEBUG2_JSON:              ,\n") ;}
              }
              const char* choice = ci.GetUTF8Text();

              OcrChar c2;
              c2.char_index = absolute_charpos;
              c2.confidence = ci.Confidence();
              c2.letter = string(choice);

              //1/17/2016 adt adding check to avoid double adding same character if ci is same as symbol. Otherwise first choice from ResultsIterator will get added twice when choiceIterator run.
              if (string(symbol) != string(choice))
                recognized_chars.push_back(c2);
              else
              {
                // Explictly double-adding the first character.  This leads to higher accuracy right now, likely because other sections of code
                // have expected it and compensated.
                // TODO: Figure out how to remove this double-counting of the first letter without impacting accuracy
                recognized_chars.push_back(c2);
              }
              if (this->config->debugOcr)
              {
                if (indent) printf("\t\t ");
                printf("\t- ");
                printf("%s conf: %f\n", choice, ci.Confidence());
                printf("\nDEBUG2_JSON:              {\"symbol\":\"%s\", \"conf\":%f }\n", choice, ci.Confidence());
              }

              indent = true;
            }
            while(ci.Next());

            if (this->config->debugOcr)
            {
              printf("\nDEBUG2_JSON:            ]\n"); //1
              printf("\nDEBUG2_JSON:            }}\n"); //2
            }
          }

          delete[] symbol;
        }
        while((ri->Next(level)));
        if (this->config->debugOcr)
        {
          printf("\nDEBUG2_JSON:        ]\n"); //3
          printf("\nDEBUG2_JSON:      }\n"); //4
        }


        delete ri;

        absolute_charpos++;
      }
    if (this->config->debugOcr)
    {
      printf("\nDEBUG2_JSON:    ]\n"); //5
      printf("\nDEBUG2_JSON:  }\n"); //6
    }

    }
    if (this->config->debugOcr)
    {
      printf("\nDEBUG2_JSON:]}\n"); //7
    }
    return recognized_chars;
  }
  void TesseractOcr::segment(PipelineData* pipeline_data) {

    CharacterSegmenter segmenter(pipeline_data);
    segmenter.segment();
  }


}
