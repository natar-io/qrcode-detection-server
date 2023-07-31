#include <iostream>
#include <vector>

#include <opencv2/opencv.hpp>
#include <opencv2/core.hpp>
#include <opencv2/highgui.hpp>
#include <zbar.h>

#define RAPIDJSON_HAS_STDSTRING 1

// JSON formatting library
#include <rapidjson/rapidjson.h>
#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>

#include <cxxopts.hpp>
#include <RedisImageHelper.hpp>

using namespace std;
using namespace cv;


bool VERBOSE = false;
bool STREAM_MODE = false;
bool SET_MODE = false;

std::string redisInputKey = "camera0";
std::string redisOutputKey = "camera0:markers";
std::string redisInputCameraParametersKey = "camera0";

std::string redisHost = "127.0.0.1";
int redisPort = 6379;

struct contextData {
    uint width;
    uint height;
    uint channels;
    zbar::ImageScanner* scanner;
    RedisImageHelperSync* clientSync;
};

static int parseCommandLine(cxxopts::Options options, int argc, char** argv)
{
    auto result = options.parse(argc, argv);
    if (result.count("h")) {
        std::cout << options.help({"", "Group"});
        return EXIT_FAILURE;
    }

    if (result.count("v")) {
        VERBOSE = true;
        std::cerr << "Verbose mode enabled." << std::endl;
    }

    if (result.count("i")) {
        redisInputKey = result["i"].as<std::string>();
        if (VERBOSE) {
            std::cerr << "Input key was set to `" << redisInputKey << "`." << std::endl;
        }
    }
    else {
        if (VERBOSE) {
            std::cerr << "No input key was specified. Input key was set to default (" << redisInputKey << ")." << std::endl;
        }
    }

    if (result.count("o")) {
        redisOutputKey = result["o"].as<std::string>();
        if (VERBOSE) {
            std::cerr << "Output key was set to `" << redisOutputKey << "`." << std::endl;
        }
    }
    else {
        if (VERBOSE) {
            std::cerr << "No output key was specified. Output key was set to default (" << redisOutputKey << ")." << std::endl;
        }
    }

    if (result.count("u")) {
        STREAM_MODE = false;
        SET_MODE = false;
        if (VERBOSE) {
            std::cerr << "Unique mode enabled." << std::endl;
        }
    }

    if (result.count("s")) {
        STREAM_MODE = true;
        if (VERBOSE) {
            std::cerr << "PUBLISH stream mode enabled." << std::endl;
        }
    }

    if (result.count("g")) {
        SET_MODE = true;
        if (VERBOSE) {
            std::cerr << "GET/SET stream mode enabled." << std::endl;
        }
    }

    if (result.count("redis-port")) {
        redisPort = result["redis-port"].as<int>();
        if (VERBOSE) {
            std::cerr << "Redis port set to " << redisPort << "." << std::endl;
        }
    }
    else {
        if (VERBOSE) {
            std::cerr << "No redis port specified. Redis port was set to " << redisPort << "." << std::endl;
        }
    }

    if (result.count("redis-host")) {
        redisHost = result["redis-host"].as<std::string>();
        if (VERBOSE) {
            std::cerr << "Redis host set to " << redisHost << "." << std::endl;
        }
    }
    else {
        if (VERBOSE) {
            std::cerr << "No redis host was specified. Redis host was set to " << redisHost << "." << std::endl;
        }
    }

    if (result.count("camera-parameters")) {
        redisInputCameraParametersKey = result["camera-parameters"].as<std::string>();
        if (VERBOSE) {
            std::cerr << "Camera parameters input key was set to " << redisInputCameraParametersKey << std::endl;
        }
    }
    else {
        if (VERBOSE) {
            std::cerr << "No camera parameters intput key specified. Camera parameters input key was set to " << redisInputCameraParametersKey << std::endl;
        }
    }

    if (!result.count("u") && !result.count("s") && !result.count("g")) {
        std::cerr << "You need to specify at least the stream method option with -u, -s or -g" << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

rapidjson::Value* QRCodeToJSON(float* center, vector<Point> corners, string data, rapidjson::Document::AllocatorType& allocator) {
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
    markerObj->AddMember("data", data, allocator);

    // TODO: Do something with the datas
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
        rapidjson::Value* markerObj = QRCodeToJSON(center, corners[i], datas[i], allocator);
        markersObj->PushBack(*markerObj, allocator);
        delete markerObj;
        delete[] center;
    }
    return markersObj;
}

std::string process(Image* image, zbar::ImageScanner* scanner) {
    // TODO: Skip opencv and create rgb_to_gray function to convert image->data() without needing to create 2 extras images
    Mat imageCv, gray;
    imageCv = Mat(image->height(), image->width(), CV_8UC3, image->data());
    cvtColor(imageCv, gray, cv::COLOR_RGB2GRAY);
    zbar::Image zbarImage (gray.cols, gray.rows, "Y800", gray.data, gray.cols * gray.rows);
    scanner->scan(zbarImage);

    vector<string> vData;
    vector<vector<Point>> vCorners;

    for (zbar::Image::SymbolIterator symbol = zbarImage.symbol_begin(); symbol != zbarImage.symbol_end(); ++symbol) {
        string type = symbol->get_type_name();
        string data = symbol->get_data();
        std::vector<Point> corners;
        if (symbol->get_location_size() == 4) {
            corners.push_back(Point(symbol->get_location_x(0), symbol->get_location_y(0)));
            corners.push_back(Point(symbol->get_location_x(1), symbol->get_location_y(1)));
            corners.push_back(Point(symbol->get_location_x(2), symbol->get_location_y(2)));
            corners.push_back(Point(symbol->get_location_x(3), symbol->get_location_y(3)));
        }
        vData.push_back(data);
        vCorners.push_back(corners);
    }

    rapidjson::Document jsonMarkers;
    jsonMarkers.SetObject();
    rapidjson::Document::AllocatorType &allocator = jsonMarkers.GetAllocator();
    rapidjson::Value* QRObj = QRCodesToJSON(vData, vCorners, allocator);
    jsonMarkers.AddMember("markers", *QRObj, allocator);
    delete QRObj;

    rapidjson::StringBuffer strbuf;
    rapidjson::Writer<rapidjson::StringBuffer> writer(strbuf);
    jsonMarkers.Accept(writer);
    return strbuf.GetString();
}

void onImagePublished(redisAsyncContext* c, void* rep, void* privdata) {
    redisReply *reply = (redisReply*) rep;
    if  (reply == NULL) { return; }
    if (reply->type != REDIS_REPLY_ARRAY || reply->elements != 3) {
        if (VERBOSE) {
            std::cerr << "Error: Bad reply format." << std::endl;
        }
        return;
    }

    struct contextData* data = static_cast<struct contextData*>(privdata);
    if (data == NULL) {
        if(VERBOSE) {
            std::cerr << "Error: Could not retrieve context data from private data." << std::endl;
        }
        return;
    }
    uint width = data->width;
    uint height = data->height;
    uint channels = data->channels;
    zbar::ImageScanner* scanner = data->scanner;
    RedisImageHelperSync* clientSync = data->clientSync;

    Image* image = RedisImageHelper::dataToImage(reply->element[2]->str, width, height, channels, reply->element[2]->len);
    if (image == NULL) {
        if (VERBOSE) {
            std::cerr << "Error: Could not retrieve image from data." << std::endl;
        }
        return;
    }
    std::string json = process(image, scanner);
    if (SET_MODE) {
        clientSync->setString((char*)json.c_str(), redisOutputKey);
    }
    clientSync->publishString((char*)json.c_str(), redisOutputKey);

    if (VERBOSE) {
        std::cerr << json << std::endl;
    }
    delete image;
}

int main(int argc, char** argv) {
    cxxopts::Options options("qrcode-server", "QRcode markers detection server.");
    options.add_options()
            ("redis-port", "The port to which the redis client should try to connect.", cxxopts::value<int>())
            ("redis-host", "The host adress to which the redis client should try to connect", cxxopts::value<std::string>())
            ("i, input", "The redis input key where data are going to arrive.", cxxopts::value<std::string>())
            ("o, output", "The redis output key where to set output data.", cxxopts::value<std::string>())
            ("s, stream", "Activate stream mode. In stream mode the program will constantly process input data and publish output data.")
            ("u, unique", "Activate unique mode. In unique mode the program will only read and output data one time.")
            ("g, stream-set", "Enable stream get/set mode. If stream mode is already enabled setting this will cause to publish and set the same data at the same time")
            ("c, camera-parameters", "The redis input key where camera-parameters are going to arrive.", cxxopts::value<std::string>())
            ("v, verbose", "Enable verbose mode. This will print helpfull process informations on the standard error stream.")
            ("h, help", "Print this help message.");

    if (parseCommandLine(options, argc, argv)) {
        return EXIT_FAILURE;
    }

    RedisImageHelperSync clientSync(redisHost, redisPort, redisInputKey);
    if (!clientSync.connect()) {
        std::cerr << "Cannot connect to redis server. Please ensure that a redis server is up and running." << std::endl;
        return EXIT_FAILURE;
    }

    zbar::ImageScanner scanner;
    scanner.set_config(zbar::ZBAR_NONE, zbar::ZBAR_CFG_ENABLE, 1);

    struct contextData data;
    data.width = clientSync.getInt(redisInputCameraParametersKey + ":width");
    data.height = clientSync.getInt(redisInputCameraParametersKey + ":height");
    data.channels = clientSync.getInt(redisInputCameraParametersKey + ":channels");
    if (data.width == -1 || data.height == -1 || data.channels == -1) {
        // TODO: Fix double free or corruption error when camera parameters can not be loaded.
        std::cerr << "Could not find camera parameters (width height channels). Please specify where to find them in redis with the --camera-parameters option parameters." << std::endl;
        return EXIT_FAILURE;
    }
    data.scanner = &scanner;
    data.clientSync = &clientSync;

    if (STREAM_MODE) {
        RedisImageHelperAsync clientAsync(redisHost, redisPort, redisInputKey);
        if (!clientAsync.connect()) {
            std::cerr << "Cannot connect to redis server. Please ensure that a redis server is up and running." << std::endl;
            return EXIT_FAILURE;
        }
        clientAsync.subscribe(redisInputKey, onImagePublished, static_cast<void*>(&data));
    }
    else {
        bool loop = true;
        while (loop) {
            Image* image = clientSync.getImage(data.width, data.height, data.channels);
            std::string json = process(image, data.scanner);
            clientSync.setString((char*)json.c_str(), redisOutputKey);
            std::cerr << json << std::endl;
            delete image;
            loop = SET_MODE ? true : false;
        }
    }
}
