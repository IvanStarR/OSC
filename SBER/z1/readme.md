sudo dnf install -y gcc-c++ cmake dnf-plugins-core
sudo dnf install -y python3 python3-pip python3-venv 
sudo dnf install -y python3-devel gcc
python3 -m http.server 5000

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j


#(можно оставить по умолчанию)
export OUTDIR="$(pwd)/web/static"
export CACHEDIR="$(pwd)/cache"

#(0 = авто)
export THREADS=0



./build/rpm_graph


cd web/static
python3 -m http.server 5000

http://localhost:5000