@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cmake -S . -B build-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DQUOTAFABRIC_ENABLE_CUDA=ON -DCMAKE_CUDA_COMPILER="C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.1\bin\nvcc.exe" -DCMAKE_CUDA_ARCHITECTURES=120 -DQUOTAFABRIC_BUILD_TESTS=ON -DQUOTAFABRIC_BUILD_EXAMPLES=ON -DQUOTAFABRIC_BUILD_BENCHMARKS=ON -DQUOTAFABRIC_BUILD_TOOLS=ON %*
if errorlevel 1 exit /b 1
cmake --build build-debug -j %NUMBER_OF_PROCESSORS%
