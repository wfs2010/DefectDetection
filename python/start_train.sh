cd /workspace/github/DefectDetection/python/yolov5

echo "Prepare environment..."
pip install -i https://pypi.tuna.tsinghua.edu.cn/simple -r requirements.txt

echo "Processing data..."
python ../preprocess.py

echo "Start training..."
python train.py --batch-size 2 --workers 0 --epochs 300  --device "cuda:0" --data ./data/defectdetection.yaml --hyp ./data/hyps/hyp.scratch-low.yaml --weight ../model/pretrain/yolov5s.pt --img 480 --project ../model/pretrain/ --cfg ./models/yolov5s.yaml
