#include "socket.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
using namespace std;

void writeString(string &buffer, int bufferSize, ostream &fout,
                        bool &passedHeader, bool &done, int &cached,
                        int &contentLength, int &contentDownloaded) {
        int headerEnding = buffer.find("\r\n\r\n");
        fout.write(&buffer[0], headerEnding);
        done = true;
}

void writeContentLength(string &buffer, int bufferSize, ostream &fout,
                        bool &passedHeader, bool &done, int &cached,
                        int &contentLength, int &contentDownloaded) {
    if (!passedHeader) {
        int headerEnding = buffer.find("\r\n\r\n");
        fout.write(&buffer[headerEnding + 4], bufferSize - headerEnding - 4);
        contentDownloaded -= headerEnding + 4;
        passedHeader = true;
    } else {
        if (contentDownloaded == contentLength)
            done = true;
        fout.write(&buffer[0], cached);
    }
}

void writeChunked(string &buffer, int bufferSize, ostream &fout,
                  bool &passedHeader, bool &done, int &cached,
                  int &contentLength, int &contentDownloaded) {
    int writePos = 0, writeLength = 0;
    if (!passedHeader) {
        writePos = buffer.find("\r\n\r\n") + 4;
        writeLength = bufferSize - writePos;
        passedHeader = true;
    }
    while (writePos < cached) {
        if (!contentLength) {
            char *p;
            contentLength = strtol(&buffer[writePos], &p, 16);
            if (!contentLength) {
                done = true;
                return;
            }
            writePos = buffer.find("\r\n", p - &buffer[0]) + 2;
        }
        writeLength = min(contentLength, cached - writePos);
        fout.write(&buffer[writePos], writeLength);
        writePos += writeLength;
        contentLength -= writeLength;
    }
}

string getRequest(string &domain, string &filePath, string &fileName,
                  string method, bool keepAlive) {
    string requestMessage = " HTTP/1.1\r\nConnection: ";
    requestMessage.insert(0, method + " " + filePath + "/" + fileName);
    requestMessage += keepAlive ? "keep-alive\r\n" : "close\r\n";
    /* requestMessage += "User-Agent: Mozilla/5.0 (Macintosh; Intel Mac OS X "
     */
    /* "10_15_7) AppleWebKit/605.1.15 (KHTML, like Gecko) " */
    /* "Version/16.1 Safari/605.1.15\r\n"; */
    requestMessage += "Host: " + domain + "\r\n\r\n";
    return requestMessage;
}

void addressProcess(string addressString, string &domain, string &filePath,
                    string &fileName) {
    filePath = "", fileName = "";
    if (!addressString.find("http://"))
        addressString.erase(0, 7);
    int firstSlash = addressString.find_first_of('/'),
        lastSlash = addressString.find_last_of('/');
    domain = addressString.substr(0, firstSlash);
    if (firstSlash != -1)
        filePath = addressString.substr(
            firstSlash, (lastSlash != -1 && lastSlash != firstSlash)
                            ? (lastSlash - firstSlash)
                            : -1);
    if (lastSlash != -1 && lastSlash != addressString.length() - 1)
        fileName = addressString.substr(lastSlash + 1);
}

class Socket {
  public:
    int timeout, portNum, bufferSize;

    Socket() {
#ifdef WIN32
        WSADATA d;
        WSAStartup(MAKEWORD(2, 2), &d);
#endif
        sockfd = socket(AF_INET6, SOCK_STREAM, 0);
        timeout = 5000;
        bufferSize = 1024 * 1024;
        buffer = string(bufferSize, 0);
    }

    ~Socket() {
#ifdef WIN32
        closesocket(sockfd);
        WSACleanup();
#else
        close(sockfd);
#endif
    }

    addrinfo *resolveDomain(string &domain) {
        addrinfo hints;
        addrinfo *servinfo;
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        if (getaddrinfo(domain.c_str(), "http", &hints, &servinfo))
            return NULL;
        return servinfo;
    }

    bool connect(addrinfo *addr) {
        bool status = false;
#ifdef WIN32
        u_long nonBlock = 1;
        ioctlsocket(sockfd, FIONBIO, nonBlock);
#else
        fcntl(sockfd, F_SETFL, O_NONBLOCK);
#endif
        pollfd pfds[1];
        pfds[0].fd = sockfd;
        pfds[0].events = POLLOUT;
        char ipstr[200]={0};
        for (addrinfo *p = addr; p && !status; p = p->ai_next) {
        inet_ntop(p->ai_family, addr, ipstr, sizeof ipstr);
            cout << "\n" << ipstr << "\n";
            status = !::connect(sockfd, addr->ai_addr, addr->ai_addrlen);
            int numEvent = poll(pfds, 1, timeout);
            if (!numEvent) {
                continue;
            } else if (pfds[0].revents & POLLIN) {
                status = true;
            }
        }
        freeaddrinfo(addr);
        return status;
    }

    bool connect(string domain) { return connect(resolveDomain(domain)); }

    bool sendRequest(string request) {
        bool status = false;
        pollfd pfds[1];
        pfds[0].fd = sockfd;
        pfds[0].events = POLLOUT;
        int numEvent = poll(pfds, 1, timeout);
        if (numEvent && pfds[0].revents & POLLOUT) {
            ::send(sockfd, request.c_str(), request.length(), 0);
            status = true;
        }
        return status;
    }


    bool readResponse(ostream& fout,void (*writeFunc)(string &, int, ostream &, bool &,
                                        bool &, int &, int &, int &)) {
        bool done = false, passedHeader = false;
        int contentLength = 0, contentDownloaded = 0, cached = 0, n = 1;
        pollfd pfds[1];
        pfds[0].fd = sockfd;
        pfds[0].events = POLLIN;
        while (!done) {
            if (bufferSize - cached > 0) {
                int numEvent = poll(pfds, 1, timeout);
                if (numEvent && pfds[0].revents & POLLIN)
                    n = recv(sockfd, &buffer[cached], bufferSize - cached, 0);
                else
                    return false;
                cached += n;
                contentDownloaded += n;
                if (done) {
                    goto writeLast;
                }
            } else {
            writeLast:
                writeFunc(buffer, bufferSize, fout, passedHeader, done, cached,
                          contentLength, contentDownloaded);
                cached = 0;
            }
        }
        return true;
    }

    bool download(string url){
        bool status=false;
        string domain, filePath, addressString, fileName;
        addressProcess(url,domain,filePath,fileName);
        ostringstream strstrm;
        ofstream fout(domain + "_" + (fileName.length() ? fileName : "index.html"));
        sendRequest(getRequest(domain, filePath, fileName, "HEAD", 1));
        readResponse(strstrm,writeString);
        string header=strstrm.str();
        sendRequest(getRequest(domain,filePath,fileName,"GET",1));
        if (header.find("chunked")){
            readResponse(fout,writeChunked);
        }
        else{
            readResponse(fout,writeContentLength);
        }
        return status;
    }


  private:
    int sockfd;
    string buffer;
};

int main() {
    Socket s;
    cout << boolalpha << s.connect("www.example.com");
    s.download("www.example.com/index.html");
}
