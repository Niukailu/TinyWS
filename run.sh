[ -d "build" ] || mkdir build

cd build
cmake ..
make
cd ..
echo "Build over"

#echo "The WebServer run..."

#./bin/tinyws
