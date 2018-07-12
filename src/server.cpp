#include <iostream>
#include <vector>

#include <opencv2/opencv.hpp>
#include <opencv2/core.hpp>
#include <opencv2/highgui.hpp>

#include <zbar.h>

// JSON formatting library
#include <rapidjson/rapidjson.h>
#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>

using namespace std;
using namespace cv;
using namespace zbar;

rapidjson::Value* QRCodeToJSON(float* center, vector<Point> corners, rapidjson::Document::AllocatorType& allocator) {
    rapidjson::Value* markerObj = new rapidjson::Value(rapidjson::kObjectType);
    markerObj->AddMember("id", -1, allocator);
    markerObj->AddMember("dir", -1, allocator);
    markerObj->AddMember("confidence", 100, allocator);
    markerObj->AddMember("type", "QRCode", allocator);

    rapidjson::Value centerArray (rapidjson::kArrayType);
    centerArray.PushBack(center[0], allocator);
    centerArray.PushBack(center[1], allocator);
    markerObj->AddMember("center", centerArray, allocator);


    rapidjson::Value cornerArray (rapidjson::kArrayType);
    // WARNING: corners should be pushed in the following order:
    // top left - top right - bot right - bot left
    cornerArray.PushBack(corners[0].x, allocator);
    cornerArray.PushBack(corners[0].y, allocator);

    cornerArray.PushBack(corners[1].x, allocator);
    cornerArray.PushBack(corners[1].y, allocator);

    cornerArray.PushBack(corners[2].x, allocator);
    cornerArray.PushBack(corners[2].y, allocator);

    cornerArray.PushBack(corners[3].x, allocator);
    cornerArray.PushBack(corners[3].y, allocator);

    markerObj->AddMember("corners", cornerArray, allocator);
    return markerObj;
}

rapidjson::Value* QRCodesToJSON(vector<string> datas, vector<vector<Point>> corners, rapidjson::Document::AllocatorType& allocator) {
    // TODO: Do something with the datas
    rapidjson::Value* markersObj = new rapidjson::Value(rapidjson::kArrayType);
    for (int i = 0 ; i < corners.size() ; i++) {
        float* center = new float[2]; center[0] = 0 ; center[1] = 0;
        for (int j = 0 ; j < corners[i].size() ; j++) {
            center[0] += corners[i][j].x;
            center[1] += corners[i][j].y;
        }
        center[0] /= corners[i].size();
        center[1] /= corners[i].size();
        rapidjson::Value* markerObj = QRCodeToJSON(center, corners[i], allocator);
        markersObj->PushBack(*markerObj, allocator);
        delete markerObj;
        delete[] center;
    }
    return markersObj;
}

int main(int argc, char** argv) {
    ImageScanner scanner;
    scanner.set_config(ZBAR_NONE, ZBAR_CFG_ENABLE, 1);

    Mat frame, frame_grayscale;
    frame = imread(argv[1], CV_LOAD_IMAGE_COLOR); cvtColor(frame, frame_grayscale, CV_BGR2GRAY);
    uchar* gray_data = frame_grayscale.data;

    Image image(frame.cols, frame.rows, "Y800", gray_data, frame.cols * frame.rows);
    scanner.scan(image);

    vector<string> datas;
    vector<vector<Point>> globCorners;

    for (Image::SymbolIterator symbol = image.symbol_begin(); symbol != image.symbol_end(); ++symbol) {
        string type = symbol->get_type_name();
        string data = symbol->get_data();
        std::vector<Point> corners;
        if (symbol->get_location_size() == 4) {
            corners.push_back(Point(symbol->get_location_x(0), symbol->get_location_y(0)));
            corners.push_back(Point(symbol->get_location_x(1), symbol->get_location_y(1)));
            corners.push_back(Point(symbol->get_location_x(2), symbol->get_location_y(2)));
            corners.push_back(Point(symbol->get_location_x(3), symbol->get_location_y(3)));
        }

        std::cout << data << std::endl;
        std::cout << corners << std::endl;
        datas.push_back(data);
        globCorners.push_back(corners);
    }

    rapidjson::Document jsonMarkers;
    jsonMarkers.SetObject();
    rapidjson::Document::AllocatorType &allocator = jsonMarkers.GetAllocator();
    rapidjson::Value* QRObj = QRCodesToJSON(datas, globCorners, allocator);
    jsonMarkers.AddMember("markers", *QRObj, allocator);
    delete QRObj;

    rapidjson::StringBuffer strbuf;
    rapidjson::Writer<rapidjson::StringBuffer> writer(strbuf);
    jsonMarkers.Accept(writer);

    cout << strbuf.GetString() << endl;
}
