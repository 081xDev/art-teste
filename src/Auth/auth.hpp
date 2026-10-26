//#include <string>
//#include <windows.h>
//#include <Security/Api/curl/curl.h>
//#include <iostream>
//#include <random>
//
//namespace auth {
//
//    inline std::string getHWID() {
//        DWORD serial = 0;
//        GetVolumeInformationA("C:\\", nullptr, 0, &serial, nullptr, nullptr, nullptr, 0);
//        return std::to_string(serial);
//    }
//
//    // Callback para capturar a resposta HTTP em uma string
//    static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
//        size_t totalSize = size * nmemb;
//        std::string* response = reinterpret_cast<std::string*>(userp);
//        response->append(reinterpret_cast<char*>(contents), totalSize);
//        return totalSize;
//    }
//
//    inline bool validate_discord_id(const std::string& discord_id) {
//        std::string hwid = getHWID();
//        std::string url = "http://31.97.255.112:5555/api/cheat/login?userId=" + discord_id + "&hwid=" + hwid;
//
//        CURL* curl = curl_easy_init();
//        if (!curl) return false;
//
//        struct curl_slist* headers = nullptr;
//        // Header exato que sua API espera
//        headers = curl_slist_append(headers, "User-Agent: know");
//
//
//        std::string response_string;
//
//        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
//        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
//        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
//        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
//
//        // Captura a resposta na response_string
//        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
//        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_string);
//
//        CURLcode res = curl_easy_perform(curl);
//        long http_code = 0;
//        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
//
//        curl_easy_cleanup(curl);
//        curl_slist_free_all(headers);
//
//        std::cout << "HTTP Status: " << http_code << "\n";
//        std::cout << "Response: " << response_string << "\n";
//
//        return (res == CURLE_OK && http_code == 200);
//    }
//
//    inline bool is_api_online() {
//        CURL* curl = curl_easy_init();
//        if (!curl) return false;
//
//        curl_easy_setopt(curl, CURLOPT_URL, "http://31.97.255.112:5555");
//        curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
//        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
//
//        CURLcode res = curl_easy_perform(curl);
//        curl_easy_cleanup(curl);
//
//        return res == CURLE_OK;
//    }
//
//}
