// server.cpp
#define _WIN32_WINNT 0x0A00
#define WINVER 0x0A00
#define NOMINMAX

#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <chrono>
#include <ctime>
#include <mutex>
#include "httplib.h"

using namespace std;
using namespace std::chrono;

// === Struktur Data ===
struct Mahasiswa {
    int npm;
    string nama;
    string jurusan;

    Mahasiswa(int n, string nm, string j) : npm(n), nama(nm), jurusan(j) {}
};

// === Database global (dilindungi mutex karena shared) ===
vector<Mahasiswa> database;
mutex db_mutex;

// === Generate database dinamis ===
vector<Mahasiswa> generateDatabase(int size) {
    vector<Mahasiswa> db;
    vector<string> jurusanList = {
        "Teknik Informatika", "Sistem Informasi", "Ilmu Komputer",
        "Teknik Komputer", "Teknik Elektro", "Statistika"
    };
    db.reserve(size); // optimasi memory
    for (int i = 1; i <= size; ++i) {
        db.emplace_back(
            20250000 + i,
            "Mahasiswa " + to_string(i),
            jurusanList[i % jurusanList.size()]
        );
    }
    // Urutkan untuk binary search
    sort(db.begin(), db.end(), [](const Mahasiswa& a, const Mahasiswa& b) {
        return a.npm < b.npm;
    });
    return db;
}

// === Algoritma Pencarian ===
int linearSearch(const vector<Mahasiswa>& db, int npm) {
    for (size_t i = 0; i < db.size(); ++i) {
        if (db[i].npm == npm) return static_cast<int>(i);
    }
    return -1;
}

int binarySearchHelper(const vector<Mahasiswa>& db, int left, int right, int npm) {
    if (left > right) return -1;
    int mid = left + (right - left) / 2;
    if (db[mid].npm == npm) return mid;
    if (db[mid].npm < npm)
        return binarySearchHelper(db, mid + 1, right, npm);
    return binarySearchHelper(db, left, mid - 1, npm);
}

int binarySearch(const vector<Mahasiswa>& db, int npm) {
    return binarySearchHelper(db, 0, static_cast<int>(db.size()) - 1, npm);
}

// === Main Server ===
int main() {
    // Generate awal: 1000 data
    {
        lock_guard<mutex> lock(db_mutex);
        database = generateDatabase(1000);
    }

    httplib::Server svr;

    // === Endpoint: Generate Database Baru ===
    svr.Post("/api/generate", [](const httplib::Request& req, httplib::Response& res) {
        if (!req.has_param("size")) {
            res.status = 400;
            res.set_content(R"({"error": "Parameter 'size' diperlukan"})", "application/json");
            return;
        }

        int size;
        try {
            size = stoi(req.get_param_value("size"));
            if (size < 1 || size > 50000) {
                throw invalid_argument("Ukuran tidak valid");
            }
        } catch (...) {
            res.status = 400;
            res.set_content(R"({"error": "Ukuran harus angka antara 1-50000"})", "application/json");
            return;
        }

        auto start = high_resolution_clock::now();
        vector<Mahasiswa> newDb = generateDatabase(size);
        auto end = high_resolution_clock::now();
        auto genTime = duration_cast<microseconds>(end - start).count();

        {
            lock_guard<mutex> lock(db_mutex);
            database = move(newDb);
        }

        cout << "🆕 [GENERATE] Database baru: " << size << " mahasiswa | Waktu: " << genTime << " µs" << endl;

        string json = R"({"success": true, "size": )" + to_string(size) +
                     R"(, "generate_time_microdetik": )" + to_string(genTime) + R"(})";
        res.set_content(json, "10.000application/json");
    });

    // === Endpoint: Cari Mahasiswa ===
    svr.Get("/api/cari", [](const httplib::Request& req, httplib::Response& res) {
        if (!req.has_param("npm")) {
            res.status = 400;
            res.set_content(R"({"error": "Parameter 'npm' diperlukan", "waktu_microdetik": 0})", "application/json");
            cout << "⚠️ [INVALID] Request tanpa parameter 'npm'" << endl;
            return;
        }

        int npm;
        try {
            npm = stoi(req.get_param_value("npm"));
        } catch (...) {
            res.status = 400;
            res.set_content(R"({"error": "NPM harus berupa angka", "waktu_microdetik": 0})", "application/json");
            cout << "⚠️ [INVALID] NPM tidak valid: " << req.get_param_value("npm") << endl;
            return;
        }

        string type = "linear";
        if (req.has_param("type")) {
            type = req.get_param_value("type");
        }

        vector<Mahasiswa> localDb;
        {
            lock_guard<mutex> lock(db_mutex);
            localDb = database; // copy aman
        }

        auto start = high_resolution_clock::now();
        int index = -1;

        if (type == "binary") {
            index = binarySearch(localDb, npm);
        } else {
            index = linearSearch(localDb, npm);
        }

        auto end = high_resolution_clock::now();
        auto duration = duration_cast<microseconds>(end - start).count();

        // === Log ke terminal ===
        auto now = system_clock::now();
        auto time_t = system_clock::to_time_t(now);
        auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;
        char buffer[20];
        strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", localtime(&time_t));
        string ms_str = (ms.count() < 10 ? "00" : (ms.count() < 100 ? "0" : "")) + to_string(ms.count());
        string waktu_str = string(buffer) + "." + ms_str;

        string status = (index != -1) ? "Ditemukan" : "Tidak ditemukan";
        string algoLog = (type == "binary") ? "Rekursif" : "Iteratif";

        cout << "🔍 [" << waktu_str << "] "
             << "Cari NPM=" << npm
             << " | Algoritma=" << algoLog
             << " | Ukuran Data=" << localDb.size()
             << " | Waktu=" << duration << " µs"
             << " | Hasil=" << status
             << endl;

        // === Respons ke frontend ===
        if (index != -1) {
            const auto& m = localDb[index];
            string algoName = (type == "binary") ? "Rekursif (Binary Search)" : "Iteratif (Linear Search)";
            string json = R"({"found": true, "npm": )" + to_string(m.npm) +
                         R"(, "nama": ")" + m.nama +
                         R"(", "jurusan": ")" + m.jurusan +
                         R"(", "algo": ")" + algoName +
                         R"(", "waktu_microdetik": )" + to_string(duration) +
                         R"(, "ukuran_data": )" + to_string(localDb.size()) + R"(})";
            res.set_content(json, "application/json");
        } else {
            string algoName = (type == "binary") ? "Rekursif" : "Iteratif";
            string json = R"({"found": false, "error": "Mahasiswa tidak ditemukan", "algo": ")" + algoName +
                         R"(", "waktu_microdetik": )" + to_string(duration) +
                         R"(, "ukuran_data": )" + to_string(localDb.size()) + R"(})";
            res.set_content(json, "application/json");
        }
    });

    svr.set_mount_point("/", "./public");

    cout << "\n✅ SISTEM PENCARIAN MAHASISWA - STUDI KASUS ALGORITMA\n";
    cout << "====================================================\n";
    cout << "🌐 Server: http://localhost:8080\n";
    cout << "📊 Ukuran awal: 1.000 mahasiswa\n";
    cout << "💡 Fitur: Ubah ukuran data langsung dari UI!\n";
    cout << "----------------------------------------------------\n\n";

    svr.listen("localhost", 8080);
}