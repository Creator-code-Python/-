#include <iostream>
#include <fstream>
#include <string>
#include <filesystem>
#include <vector>
#include <cstdlib>
#include <algorithm>

#ifdef _WIN32
    #include <windows.h>
    #include <wininet.h>
    #pragma comment(lib, "wininet.lib")
#else
    #include <curl/curl.h>
#endif

#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using json = nlohmann::json;

// ============ Конфигурация ============
const std::string CONFIG_URL = "https://raw.githubusercontent.com/username/repo/main/config.json";
const std::string INSTALL_DIR = []() {
    std::string home;
#ifdef _WIN32
    home = std::getenv("USERPROFILE");
#else
    home = std::getenv("HOME");
#endif
    return home + "/MyLauncher";
}();

// ============ HTTP Request ============
size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* output) {
    size_t totalSize = size * nmemb;
    output->append(static_cast<char*>(contents), totalSize);
    return totalSize;
}

bool http_get(const std::string& url, std::string& response) {
#ifdef _WIN32
    HINTERNET hInternet = InternetOpenA("MyLauncher/1.0", INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
    if (!hInternet) return false;

    HINTERNET hUrl = InternetOpenUrlA(hInternet, url.c_str(), NULL, 0, INTERNET_FLAG_RELOAD, 0);
    if (!hUrl) {
        InternetCloseHandle(hInternet);
        return false;
    }

    char buffer[4096];
    DWORD bytesRead;
    while (InternetReadFile(hUrl, buffer, sizeof(buffer) - 1, &bytesRead) && bytesRead > 0) {
        buffer[bytesRead] = '\0';
        response += buffer;
    }

    InternetCloseHandle(hUrl);
    InternetCloseHandle(hInternet);
    return true;
#else
    CURL* curl = curl_easy_init();
    if (!curl) return false;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    
    return res == CURLE_OK;
#endif
}

// ============ Загрузка файла ============
bool download_file(const std::string& url, const std::string& path) {
    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) return false;

#ifdef _WIN32
    HINTERNET hInternet = InternetOpenA("MyLauncher/1.0", INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
    if (!hInternet) return false;

    HINTERNET hUrl = InternetOpenUrlA(hInternet, url.c_str(), NULL, 0, INTERNET_FLAG_RELOAD, 0);
    if (!hUrl) {
        InternetCloseHandle(hInternet);
        return false;
    }

    char buffer[8192];
    DWORD bytesRead;
    while (InternetReadFile(hUrl, buffer, sizeof(buffer), &bytesRead) && bytesRead > 0) {
        file.write(buffer, bytesRead);
    }

    InternetCloseHandle(hUrl);
    InternetCloseHandle(hInternet);
#else
    CURL* curl = curl_easy_init();
    if (!curl) return false;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &file);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, [](void* ptr, size_t size, size_t nmemb, void* stream) -> size_t {
        std::ofstream* f = static_cast<std::ofstream*>(stream);
        f->write(static_cast<char*>(ptr), size * nmemb);
        return size * nmemb;
    });
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 300L);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    
    if (res != CURLE_OK) {
        file.close();
        return false;
    }
#endif

    file.close();
    return true;
}

// ============ Загрузка конфига с GitHub ============
json load_config() {
    std::string response;
    if (!http_get(CONFIG_URL, response)) {
        std::cerr << "[!] Ошибка загрузки конфигурации\n";
        return nullptr;
    }

    try {
        return json::parse(response);
    } catch (const std::exception& e) {
        std::cerr << "[!] Ошибка парсинга JSON: " << e.what() << "\n";
        return nullptr;
    }
}

// ============ Скачивание одного файла ============
void download(const json& file) {
    try {
        std::string name = file["name"];
        std::string filename = file["filename"];
        std::string url = file["url"];
        
        fs::path path = fs::path(INSTALL_DIR) / filename;
        
        std::cout << "[+] Скачивание " << name << "...\n";
        
        if (download_file(url, path.string())) {
            std::cout << "[✓] Установлено: " << path << "\n";
        } else {
            std::cerr << "[!] Ошибка скачивания: " << name << "\n";
        }
    } catch (const std::exception& e) {
        std::cerr << "[!] Ошибка: " << e.what() << "\n";
    }
}

// ============ Подменю выбора файлов ============
void submenu(const std::vector<json>& files_list) {
    while (true) {
        std::cout << "\n--- Выберите что скачивать ---\n";
        for (size_t i = 0; i < files_list.size(); i++) {
            std::cout << i + 1 << ". " << files_list[i]["name"] << "\n";
        }
        std::cout << "0. Назад\n";

        std::cout << "Выбор: ";
        std::string choice_str;
        std::getline(std::cin, choice_str);
        
        if (choice_str == "0") break;
        
        try {
            int choice = std::stoi(choice_str);
            if (choice >= 1 && choice <= static_cast<int>(files_list.size())) {
                download(files_list[choice - 1]);
            } else {
                std::cout << "Неверный выбор!\n";
            }
        } catch (...) {
            std::cout << "Ошибка ввода!\n";
        }
    }
}

// ============ Фильтрация файлов по типу ============
std::vector<json> filter_files(const json& config, const std::string& keyword) {
    std::vector<json> result;
    
    if (config.contains("files")) {
        for (const auto& f : config["files"]) {
            std::string filename = f.value("filename", "");
            std::string name = f.value("name", "");
            
            std::string lower_filename = filename;
            std::string lower_name = name;
            std::transform(lower_filename.begin(), lower_filename.end(), lower_filename.begin(), ::tolower);
            std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);
            
            if (lower_filename.find(keyword) != std::string::npos || 
                lower_name.find(keyword) != std::string::npos) {
                result.push_back(f);
            }
        }
    }
    
    return result;
}

// ============ Главное меню ============
void menu() {
    // Создаём папку установки
    if (!fs::exists(INSTALL_DIR)) {
        fs::create_directories(INSTALL_DIR);
        std::cout << "[+] Создана папка: " << INSTALL_DIR << "\n";
    }

    while (true) {
        json config = load_config();
        if (config.is_null()) {
            std::cout << "Нажмите Enter для выхода...";
            std::cin.get();
            return;
        }

        std::cout << "\n=== LAUNCHER v" << config.value("version", "1.0") << " ===\n";
        std::cout << "1. Скачать CFG\n";
        std::cout << "2. Скачать Skin\n";
        std::cout << "3. Скачать GUI\n";
        std::cout << "4. Скачать AHK\n";
        std::cout << "9. Скачать всё\n";
        std::cout << "0. Выход\n";

        std::cout << "Выбор: ";
        std::string choice;
        std::getline(std::cin, choice);

        if (choice == "0") {
            std::cout << "До свидания!\n";
            break;
        } else if (choice == "1") {
            auto cfg_files = filter_files(config, "cfg");
            if (cfg_files.empty()) {
                std::cout << "[!] CFG файлы не найдены\n";
            } else {
                submenu(cfg_files);
            }
        } else if (choice == "2") {
            auto skin_files = filter_files(config, "skin");
            if (skin_files.empty()) {
                std::cout << "[!] Skin файлы не найдены\n";
            } else {
                submenu(skin_files);
            }
        } else if (choice == "3") {
            if (config.contains("gui")) {
                std::vector<json> gui_files = {config["gui"]};
                submenu(gui_files);
            } else {
                std::cout << "[!] GUI не найден в конфиге\n";
            }
        } else if (choice == "4") {
            if (config.contains("ahk")) {
                std::vector<json> ahk_files;
                for (const auto& f : config["ahk"]) {
                    ahk_files.push_back(f);
                }
                if (ahk_files.empty()) {
                    std::cout << "[!] AHK файлы не найдены\n";
                } else {
                    submenu(ahk_files);
                }
            } else {
                std::cout << "[!] AHK секция не найдена в конфиге\n";
            }
        } else if (choice == "9") {
            std::cout << "\n[+] Скачивание всех файлов...\n";
            
            if (config.contains("files")) {
                for (const auto& f : config["files"]) {
                    download(f);
                }
            }
            if (config.contains("gui")) {
                download(config["gui"]);
            }
            if (config.contains("ahk")) {
                for (const auto& f : config["ahk"]) {
                    download(f);
                }
            }
            
            std::cout << "\n[✓] Все файлы установлены!\n";
        } else {
            std::cout << "Неверный выбор!\n";
        }
    }
}

// ============ Инициализация ============
#ifdef _WIN32
int WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // Windows: открываем консоль если запущена как GUI
    if (AllocConsole()) {
        freopen("CONOUT$", "w", stdout);
        freopen("CONOUT$", "w", stderr);
        freopen("CONIN$", "r", stdin);
    }
#else
int main() {
#endif

    std::cout << R"(
    ╔══════════════════════════════════╗
    ║       CFG / AHK / GUI Launcher                                          ║
    ╚══════════════════════════════════╝
    )" << "\n";
    
    std::cout << "Папка установки: " << INSTALL_DIR << "\n";
    std::cout << "Конфиг: " << CONFIG_URL << "\n\n";
    
    menu();
    
#ifdef _WIN32
    std::cout << "\nНажмите Enter для выхода...";
    std::cin.get();
    FreeConsole();
#endif
    
    return 0;
}