#include <iostream>
#include <fstream>
#include <vector>
#include <thread>
#include <mutex>
#include <chrono>
#include <zlib.h>
#include <filesystem>

namespace fs = std::filesystem;
using namespace std;
using namespace std::chrono;

mutex mtx;


struct CompressionTask {
    string inputPath;
    string outputPath;
    bool compress;
    int level;
};


void processFile(const string& inputPath, const string& outputPath, bool compress, int level = Z_DEFAULT_COMPRESSION) {
    ifstream inFile(inputPath, ios::binary);
    if (!inFile) {
        lock_guard<mutex> lock(mtx);
        cerr << "Error opening input file: " << inputPath << endl;
        return;
    }

    ofstream outFile(outputPath, ios::binary);
    if (!outFile) {
        lock_guard<mutex> lock(mtx);
        cerr << "Error opening output file: " << outputPath << endl;
        return;
    }

    vector<char> inBuffer(1024 * 1024); 
    vector<char> outBuffer(1024 * 1024);

    z_stream zs = {0};
    if (compress) {
        deflateInit(&zs, level);
    } else {
        inflateInit(&zs);
    }

    while (true) {
        inFile.read(inBuffer.data(), inBuffer.size());
        size_t bytesRead = inFile.gcount();
        if (bytesRead == 0) break;

        zs.next_in = reinterpret_cast<Bytef*>(inBuffer.data());
        zs.avail_in = static_cast<uInt>(bytesRead);

        do {
            zs.next_out = reinterpret_cast<Bytef*>(outBuffer.data());
            zs.avail_out = static_cast<uInt>(outBuffer.size());

            int ret;
            if (compress) {
                ret = deflate(&zs, inFile.eof() ? Z_FINISH : Z_NO_FLUSH);
            } else {
                ret = inflate(&zs, Z_NO_FLUSH);
            }

            if (ret == Z_STREAM_ERROR) {
                lock_guard<mutex> lock(mtx);
                cerr << "Error during compression/decompression" << endl;
                return;
            }

            size_t bytesWritten = outBuffer.size() - zs.avail_out;
            outFile.write(outBuffer.data(), bytesWritten);

        } while (zs.avail_out == 0);
    }

    if (compress) {
        deflateEnd(&zs);
    } else {
        inflateEnd(&zs);
    }

    lock_guard<mutex> lock(mtx);
    cout << "Processed: " << inputPath << " -> " << outputPath << endl;
}

void processFiles(const vector<CompressionTask>& tasks, int numThreads) {
    vector<thread> threads;
    size_t currentTask = 0;
    mutex taskMutex;

    auto worker = [&]() {
        while (true) {
            size_t taskIndex;
            {
                lock_guard<mutex> lock(taskMutex);
                if (currentTask >= tasks.size()) return;
                taskIndex = currentTask++;
            }
            const auto& task = tasks[taskIndex];
            processFile(task.inputPath, task.outputPath, task.compress, task.level);
        }
    };

    for (int i = 0; i < numThreads; ++i) {
        threads.emplace_back(worker);
    }

    for (auto& t : threads) {
        t.join();
    }
}

template<typename Func>
auto measureTime(Func f) {
    auto start = high_resolution_clock::now();
    f();
    auto end = high_resolution_clock::now();
    return duration_cast<milliseconds>(end - start);
}


void displayMenu() {
    cout << "\n===== Multithreaded File Compression Tool =====" << endl;
    cout << "1. Compress file(s)" << endl;
    cout << "2. Decompress file(s)" << endl;
    cout << "3. Benchmark (compare single vs multi-threaded)" << endl;
    cout << "4. Exit" << endl;
    cout << "Enter your choice: ";
}

int main() {
    cout << "CODTECH Multithreaded File Compression Tool" << endl;
    cout << "==========================================" << endl;

    int choice;
    do {
        displayMenu();
        cin >> choice;
        cin.ignore(); 

        switch (choice) {
            case 1: {
                string inputPath, outputPath;
                int numThreads, compressionLevel;

                cout << "Enter input file/directory: ";
                getline(cin, inputPath);
                cout << "Enter output directory: ";
                getline(cin, outputPath);
                cout << "Number of threads: ";
                cin >> numThreads;
                cout << "Compression level (0-9, 0=fastest, 9=best): ";
                cin >> compressionLevel;

                vector<CompressionTask> tasks;

                if (fs::is_directory(inputPath)) {
                    for (const auto& entry : fs::directory_iterator(inputPath)) {
                        if (entry.is_regular_file()) {
                            string outFile = outputPath + "/" + entry.path().filename().string() + ".gz";
                            tasks.push_back({entry.path().string(), outFile, true, compressionLevel});
                        }
                    }
                } else {
                    string outFile = outputPath + "/" + fs::path(inputPath).filename().string() + ".gz";
                    tasks.push_back({inputPath, outFile, true, compressionLevel});
                }

                auto duration = measureTime([&]() {
                    processFiles(tasks, numThreads);
                });

                cout << "Compression completed in " << duration.count() << " ms" << endl;
                break;
            }
            case 2: {
                string inputPath, outputPath;
                int numThreads;

                cout << "Enter input file/directory: ";
                getline(cin, inputPath);
                cout << "Enter output directory: ";
                getline(cin, outputPath);
                cout << "Number of threads: ";
                cin >> numThreads;

                vector<CompressionTask> tasks;

                if (fs::is_directory(inputPath)) {
                    for (const auto& entry : fs::directory_iterator(inputPath)) {
                        if (entry.is_regular_file() && entry.path().extension() == ".gz") {
                            string outFile = outputPath + "/" + entry.path().stem().string();
                            tasks.push_back({entry.path().string(), outFile, false});
                        }
                    }
                } else {
                    string outFile = outputPath + "/" + fs::path(inputPath).stem().string();
                    tasks.push_back({inputPath, outFile, false});
                }

                auto duration = measureTime([&]() {
                    processFiles(tasks, numThreads);
                });

                cout << "Decompression completed in " << duration.count() << " ms" << endl;
                break;
            }
            case 3: {
                string testFile = "large_test_file.bin";
                string compressedFile = "compressed_test.gz";
                string decompressedFile = "decompressed_test.bin";

                
                if (!fs::exists(testFile)) {
                    cout << "Creating test file (100MB)..." << endl;
                    ofstream out(testFile, ios::binary);
                    vector<char> data(1024 * 1024); 
                    for (int i = 0; i < 100; i++) {
                        out.write(data.data(), data.size());
                    }
                }

                
                cout << "\nRunning single-threaded test..." << endl;
                auto singleThreadTime = measureTime([&]() {
                    vector<CompressionTask> task = {{testFile, compressedFile, true}};
                    processFiles(task, 1);
                });

                
                cout << "\nRunning multi-threaded test (4 threads)..." << endl;
                auto multiThreadTime = measureTime([&]() {
                    vector<CompressionTask> tasks;
                    for (int i = 0; i < 4; i++) {
                        tasks.push_back({testFile + to_string(i), compressedFile + to_string(i), true});
                    }
                    processFiles(tasks, 4);
                });

                cout << "\nBenchmark Results:" << endl;
                cout << "Single-threaded time: " << singleThreadTime.count() << " ms" << endl;
                cout << "Multi-threaded time: " << multiThreadTime.count() << " ms" << endl;
                cout << "Performance gain: " 
                     << (1.0 - static_cast<double>(multiThreadTime.count()) / singleThreadTime.count()) * 100 
                     << "% faster" << endl;
                break;
            }
            case 4:
                cout << "Exiting program..." << endl;
                break;
            default:
                cout << "Invalid choice!" << endl;
        }
    } while (choice != 4);

    return 0;
}
