# DefectDetection

# prepare 

## Model and algorithm

# Yolov5 model
You can download the yolo pretrain model in the repo(https://github.com/ultralytics/yolov5)

# dataset 
You can get t


```json
{
    "algorithm_data": {
        "is_alert": true,
        "target_count": 1,
        "target_info": [
            {
                "x": 0,
                "y": 259,
                "width": 396,
                "height": 121,
                "confidence": 0.373779296875,
                "name": "photovoltaic_panels"
            }
        ]
    },
    "model_data": {
        "objects": [
            {
                "x": 0,
                "y": 259,
                "width": 396,
                "height": 121,
                "confidence": 0.373779296875,
                "name": "photovoltaic_panels"
            }
        ]
    }
}

```

## Deploy

### install library dependency 
```shell
# opencv
cd  ./lib
apt install build-essential cmake ffmpeg unzip build-essential libboost-all-dev
wget https://github.com/opencv/opencv/archive/4.5.5.zip
unzip 4.5.5.zip
cd ./opencv-4.5.5
mkdir build && cd build
cmake .. 
make -j8  && make install

# glog
cd ./lib
git clone  https://github.com/google/glog.git
cd glog/
mkdir build && cd build
cmake ..
make -j8  && make install

# gtest
cd ./lib
git clone https://github.com/google/googletest.git
cd googletest/
mkdir build && cd build
cmake ..
make -j8  && make install
```

### install our project
```shell
# compile SDK library
mkdir build && cd build
cmake .. && make install
# compile test tools
mkdir ./test/build && cd ./test/build
cmake .. && make install
# test 
./bin/test-ji-api -f 1 -i ../data/vp.jpeg -o result.jpg
```
### results

## contributor
- [wfs2010](https://github.com/wfs2010)

## Thanks
Excellent computer vision learning platform: https://www.cvmart.net/
