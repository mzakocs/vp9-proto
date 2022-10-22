cd ./webm/vp9
rm -rf ../protobuf
mkdir ../protobuf
i=0
for FILE in *;
do 
  echo $FILE
  ../../../bazel-bin/vp9_to_proto $FILE ../protobuf/$i-proto
  let "i+=1"
done;