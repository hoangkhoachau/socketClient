#include "socket.h"
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <queue>
#include <sstream>
#include <string>
#include <vector>
using namespace std;

enum class linkType { folder, file, unknown };

vector<string> getLinksOfFolder(string &url, string html) {
    int lastFound = 0, quote = 0;
    vector<string> links;
    while (lastFound != -1) {
        lastFound = html.find("href=\"", lastFound + 6);
        quote = html.find('"', lastFound + 6);
        if (lastFound != -1 && isalnum(html[lastFound + 6]) &&
            html.substr(lastFound + 6, 4) != "http")
            links.push_back(url +
                            html.substr(lastFound + 6, quote - lastFound - 6));
    }
    return links;
}

addrinfo *resolveDomain(string domain) {
    addrinfo hints;
    addrinfo *res;
    /* cout << "Resolving " << domain << " ... "; */
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(&domain[0], "http", &hints, &res)) {
        /* cout << "failed\n"; */
        return NULL;
    }
    return res;
}

void writeContentLength(string &buffer, int start, int end, ostream &fout,
                        bool &done, int &contentDownloaded,
                        int &contentLength) {
    fout.write(&buffer[start], end - start);
}

void writeChunked(string &buffer, int start, int end, ostream &fout, bool &done,
                  int &contentDownloaded, int &contentLength) {
    int writePos = start, writeLength = 0;
    while (writePos < end) {
        if (!contentLength) {
            char *p;
            contentLength = strtol(&buffer[writePos], &p, 16);
            done = !contentLength;
            if (done)
                return;
            contentDownloaded += contentLength;
            writePos = buffer.find("\r\n", p - &buffer[0]) + 2;
        }
        writeLength = min(contentLength, end - writePos);
        fout.write(&buffer[writePos], writeLength);
        writePos += writeLength;
        contentLength -= writeLength;
    }
}

string getRequest(string &domain, string &filePath, string &fileName,
                  string method, bool keepAlive, linkType type) {
    string requestMessage = " HTTP/1.1\r\nConnection: ";
    if (type == linkType::folder)
        requestMessage.insert(0, "/");
    if (type == linkType::unknown && filePath.empty())
        filePath = "/";
    requestMessage.insert(0, method + " " + filePath + fileName);
    requestMessage += keepAlive ? "keep-alive\r\n" : "close\r\n";
    requestMessage += "Host: " + domain + "\r\n\r\n";
    return requestMessage;
}

void addressProcess(string addressString, string &domain, string &filePath,
                    string &fileName, linkType &type) {
    filePath = "", fileName = "";
    if (!addressString.find("http://"))
        addressString.erase(0, 7);
    int firstSlash = addressString.find_first_of('/'),
        lastSlash = addressString.find_last_of('/');
    if (firstSlash != -1) {
        if (addressString.back() == '/') {
            type = linkType::folder;
            addressString.pop_back();
            lastSlash = addressString.find_last_of('/');
        } else {
            if (addressString.find('.', lastSlash) != -1)
                type = linkType::file;
            else
                type = linkType::unknown;
        }
        if (lastSlash != -1) {
            filePath =
                addressString.substr(firstSlash, lastSlash - firstSlash + 1);
            fileName = addressString.substr(lastSlash + 1);
        }
    } else
        type = linkType::unknown;
    domain = addressString.substr(0, firstSlash);
}

class Socket {
  public:
    int timeout, portNum, bufferSize;
    bool connected;
    string domain;

    Socket() {
#ifdef WIN32
        WSADATA d;
        WSAStartup(MAKEWORD(2, 2), &d);
#endif
        timeout = 5000;
        bufferSize = 2 * 1024 * 1024;
        buffer = string(bufferSize, 0);
        connected = false;
    }

    ~Socket() {
#ifdef WIN32
        closesocket(sockfd);
        WSACleanup();
#else
        close(sockfd);
#endif
    }

    bool connect(addrinfo *addr) {
#ifdef WIN32
        u_long nonBlock = 1;
        ioctlsocket(sockfd, FIONBIO, &nonBlock);
#else
        fcntl(sockfd, F_SETFL, O_NONBLOCK);
#endif
        pollfd pfds[1];
        pfds[0].events = POLLOUT;
        for (addrinfo *p = addr; p && !connected; p = p->ai_next) {
            sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
            pfds[0].fd = sockfd;
            /* cout << "Connecting... "; */
            connected = !::connect(sockfd, p->ai_addr, p->ai_addrlen);
#ifdef WIN32
            int numEvent = WSAPoll(pfds, 1, timeout);
#else
            int numEvent = poll(pfds, 1, timeout);
#endif
            if (numEvent && pfds[0].revents & POLLOUT) {
                /* cout << "connected\n"; */
                connected = true;
                break;
            }
            /* cout << "failed\n"; */
        }
        freeaddrinfo(addr);
        return connected;
    }

    bool connect(string domain) {
        addrinfo *t = (resolveDomain(domain));
        if (!t)
            return false;
        return connect(t);
    }

    bool sendRequest(string &request) {
        bool status = false;
        pollfd pfds[1];
        pfds[0].fd = sockfd;
        pfds[0].events = POLLOUT;
#ifdef WIN32
        int numEvent = WSAPoll(pfds, 1, timeout);
#else
        int numEvent = poll(pfds, 1, timeout);
#endif
        if (numEvent && pfds[0].revents & POLLOUT) {
            ::send(sockfd, request.c_str(), request.length(), 0);
            status = true;
        }
        return status;
    }

    bool readResponse(ostream &fout) {
        bool done = false, passedHeader = false, chunked = false;
        int contentLength = 0, contentDownloaded = 0, cached = 0, n, start = 0;
        pollfd pfds[1];
        pfds[0].fd = sockfd;
        pfds[0].events = POLLIN;
        while (!done) {
            if (bufferSize - cached > 0) {
#ifdef WIN32
                int numEvent = WSAPoll(pfds, 1, timeout);
#else
                int numEvent = poll(pfds, 1, timeout);
#endif
                if (numEvent && pfds[0].revents & POLLIN) {
                    n = recv(sockfd, &buffer[cached], bufferSize - cached, 0);
                    cached += n;
                    /* cout << n << '\n'; */
                } else
                    return false;
                contentDownloaded += n;
                if (!passedHeader) {
                    int headerEnding =
                        buffer.find("\r\n\r\n", max(cached - 4 - n, 0));
                    if (headerEnding != -1) {
                        chunked = buffer.find("chunked") != -1;
                        if (!chunked) {
                            int contentLengthPos =
                                buffer.find("Content-Length: ") + 16;
                            contentLength =
                                strtol(&buffer[contentLengthPos], 0, 10);
                        }
                        passedHeader = true;
                        start = headerEnding + 4;
                        contentDownloaded -= start;
                    }
                }
                if ((chunked && buffer[cached - 1] == '\n' &&
                     buffer[cached - 2] == '\r' && buffer[cached - 3] == '\n' &&
                     buffer[cached - 4] == '\r') ||
                    (!chunked && contentDownloaded >= contentLength))
                    done = true;
                if (done || n == 0) {
                    goto writeLast;
                }
            } else {
            writeLast:
                if (chunked)
                    writeChunked(buffer, start, cached, fout, done,
                                 contentDownloaded, contentLength);
                else
                    writeContentLength(buffer, start, cached, fout, done,
                                       contentDownloaded, contentLength);
                start = 0, cached = 0;
            }
        }
        return true;
    }

    bool download(string &url, string path) {
        string filePath, addressString, fileName, request, html;
        ofstream fout;
        linkType type;
        if (!filesystem::exists(path))
            filesystem::create_directory(path);
        addressProcess(url, domain, filePath, fileName, type);
        if (!this->connected && !connect(domain))
            return false;
        cout << fileName << ' ' << filePath << " \033[93mdownloading\033[0m"
             << '\n';
        request = getRequest(domain, filePath, fileName, "GET", true, type);
        /* cout << request << '\n'; */
        if (!sendRequest(request))
            return false;
        switch (type) {
        case linkType::file:
            fout.open(path + ((path == "./") ? (domain + "_") : "") + fileName,
                      ios::binary);
            break;
        case linkType::folder:
        case linkType::unknown:
            if (fileName.empty())
                fileName = "index";
            fout.open(path + ((path == "./") ? (domain + "_") : "") + fileName +
                          ".html",
                      ios::binary);
        }
        if (!readResponse(fout))
            return false;
        fout.close();
        if (type == linkType::folder) {
            ifstream folderHtml(path + domain + "_" + fileName + ".html");
            ostringstream ss;
            ss << folderHtml.rdbuf();
            vector<string> links = getLinksOfFolder(url, ss.str());
            for (string &link : links)
                this->addToQueue(link, path + domain + "_" + fileName + "/");
        }
        cout << fileName << ' ' << filePath << " \033[92mOK\n\033[0m";
        return true;
    }

    void addToQueue(string url, string path = "./") {
        downloadQueue.push({url, path});
    }

    bool processQueue() {
        int status = false;
        while (!downloadQueue.empty()) {
            status = download(downloadQueue.front().first,
                              downloadQueue.front().second);
            if (!status)
                cout << domain << "\033[91mtimeout\033[0m\n";
            downloadQueue.pop();
        }
        return status;
    };

  private:
    int sockfd;
    string buffer;
    queue<pair<string, string>> downloadQueue;
};

int main(int argc, char *argv[]) {
#ifdef WIN32
    WSADATA d;
    if (WSAStartup(MAKEWORD(2, 2), &d))
        return 0;
#endif
    vector<Socket> sockets;
    if (argc < 2) {
        cout << "Usage: " << argv[0] << " [url1] [url2]..\n";
        return 0;
    }
    for (int i = 1; i < argc; i++) {
        sockets.push_back(Socket());
        sockets.back().addToQueue(argv[i]);
    }
    vector<future<bool>> socketStatus;
    for (Socket &s : sockets)
        socketStatus.push_back(async(launch::async, &Socket::processQueue, &s));
    for (int i = 0; i < socketStatus.size(); i++)
        if (!socketStatus[i].get())
            cout << "SOCKET " << i << " \033[91mFAILED!\n\033[0m";
    return 0;
}
