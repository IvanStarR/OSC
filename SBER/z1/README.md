#Зависимости 
sudo dnf install -y git cmake gcc-c++ make \
  tinyxml2-devel libsolv-devel libxml2-devel zlib-devel xz-devel zstd \
  dnf-plugins-core 

#докачиваются автоматом через FetchContent fmt и spdlog



cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j


#Включить исходники (для build-графа):
sudo dnf config-manager --set-enabled source update-source
sudo dnf makecache --refresh
sudo dnf makecache



#Запуск
export THREADS="$(nproc)"
export OUTDIR="$(pwd)/web/static"
#у меня REPOIDS и ARCHS такие:
export REPOIDS="OS,everything,update,source,update-source"
export ARCHS="x86_64,noarch"

./build/rpm_graph_fast


#Вывод:
web/static/runtime_graph.json
web/static/build_graph.json 


