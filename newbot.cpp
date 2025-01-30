#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <iostream>
#include <fstream>
#include <cstring>
#include <string>
#include <curl/curl.h>
#include <nlohmann/json.hpp>  


void sendToTelegram(const std::string& botToken, const std::string& chatId, const std::string& message) {
   
    std::string apiUrl = "https://api.telegram.org/bot" + botToken + "/sendMessage";


    CURL* curl = curl_easy_init();
    if (curl) {

        std::string postFields = "chat_id=" + chatId + "&text=" + curl_easy_escape(curl, message.c_str(), message.length());


        curl_easy_setopt(curl, CURLOPT_URL, apiUrl.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postFields.c_str());


        CURLcode res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            std::cerr << "Ошибка при отправке сообщения в Telegram: " << curl_easy_strerror(res) << std::endl;
        }


        curl_easy_cleanup(curl);
    }
}


std::string getUpdates(const std::string& botToken, int offset = 0) {
    std::string apiUrl = "https://api.telegram.org/bot" + botToken + "/getUpdates?offset=" + std::to_string(offset);
    CURL* curl = curl_easy_init();
    std::string responseString;
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, apiUrl.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, +[](void* contents, size_t size, size_t nmemb, void* userp) -> size_t {
            ((std::string*)userp)->append((char*)contents, size * nmemb);
            return size * nmemb;
        });
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseString);
        CURLcode res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);

        if (res != CURLE_OK) {
            std::cerr << "Ошибка при получении обновлений от Telegram: " << curl_easy_strerror(res) << std::endl;
            return "";
        }
    }
    return responseString;
}

std::string translateToRussian(const std::string& data) {
    std::string translatedData = data;
    size_t pos;
    pos = translatedData.find("Temperature");
    if (pos != std::string::npos) {
        translatedData.replace(pos, std::string("Temperature").length(), "Температура");
    }

    pos = translatedData.find("Humidity");
    if (pos != std::string::npos) {
        translatedData.replace(pos, std::string("Humidity").length(), "Влажность");
    }

    return translatedData;
}

std::string extractCommand(const std::string& message) {
    size_t pos = message.find('/');
    if (pos != std::string::npos) {
        return message.substr(pos);  
    }
    return "";
}

int main() {
    const char* portName = "/dev/ttyUSB0";  
    int serialPort = open(portName, O_RDWR);

    if (serialPort == -1) {
        std::cerr << "Ошибка открытия порта " << portName << std::endl;
        return 1;
    }


    termios tty;
    memset(&tty, 0, sizeof tty);

    if (tcgetattr(serialPort, &tty) != 0) {
        std::cerr << "Ошибка получения параметров порта" << std::endl;
        close(serialPort);
        return 1;
    }

    cfsetispeed(&tty, B9600);
    cfsetospeed(&tty, B9600);

    tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;  
    tty.c_iflag &= ~IGNBRK;                      
    tty.c_lflag = 0;                             
    tty.c_oflag = 0;                             
    tty.c_cc[VMIN] = 1;
    tty.c_cc[VTIME] = 1;

    if (tcsetattr(serialPort, TCSANOW, &tty) != 0) {
        std::cerr << "Ошибка установки параметров порта" << std::endl;
        close(serialPort);
        return 1;
    }

    std::string botToken = ""; 

    std::cout << "Чтение данных с Arduino, запись в файл и отправка в Telegram..." << std::endl;

    char buffer[256];
    std::string lastData = "";  
    int lastUpdateId = 0;       

    while (true) {
        int bytesRead = read(serialPort, buffer, sizeof(buffer) - 1);
               if (bytesRead > 0) {
            buffer[bytesRead] = '\0';  
            std::string data(buffer);
            
            std::string translatedData = translateToRussian(data);
            std::cout << translatedData;
            
            if (translatedData.find("Температура") != std::string::npos && 
                translatedData.find("Влажность") != std::string::npos) {
                lastData = translatedData;  
                
                std::ofstream outFile("data_log.txt", std::ofstream::trunc);
                if (outFile.is_open()) {
                    outFile << lastData;  
                    outFile.close();
                } else {
                    std::cerr << "Ошибка при записи в файл" << std::endl;
                }
            }
        }

        std::string updates = getUpdates(botToken, lastUpdateId);

        try {
            auto jsonResponse = nlohmann::json::parse(updates);
            for (auto& update : jsonResponse["result"]) {
                int updateId = update["update_id"];
                std::string messageText = update["message"]["text"];
                std::string chatId;

                
                if (update["message"]["chat"]["id"].is_number()) {
                    chatId = std::to_string(update["message"]["chat"]["id"].get<int>());  
                } else if (update["message"]["chat"]["id"].is_string()) {
                    chatId = update["message"]["chat"]["id"].get<std::string>();  
                }

               
                if (updateId > lastUpdateId) {
                    lastUpdateId = updateId;  

                    if (messageText == "/th") {
                        
                        if (!lastData.empty()) {
                            sendToTelegram(botToken, chatId, lastData);  
                        } else {
                            sendToTelegram(botToken, chatId, "Нет данных о температуре и влажности.");
                        }
                    }
                }
            }
        } catch (const nlohmann::json::parse_error& e) {
            std::cerr << "Ошибка парсинга JSON: " << e.what() << std::endl;
        }

        
        sleep(3);
    }
    close(serialPort);

    return 0;
}

