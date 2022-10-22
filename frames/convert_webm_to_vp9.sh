cd ./webm
rm -rf ./vp9
mkdir ./vp9
i=0
for FILE in *;
do 
  ffmpeg -i file:$FILE -vcodec libvpx-vp9 -an -f ivf ./vp9/$i.ivf
  let "i+=1"
done;