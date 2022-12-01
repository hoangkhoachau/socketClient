# socketClient

## Overview
This project is a socket client implemented in C++. It demonstrates the use of socket programming and client-server communication. The client can resolve domains, connect to servers, send HTTP requests, and download files or folders.

## Features
- Resolve domain names to IP addresses.
- Connect to servers using sockets.
- Send HTTP GET requests.
- Download files and folders from servers.
- Handle chunked transfer encoding and content length.
- Multi-threaded download for multiple URLs.

## Setup
To set up and run this project, follow these steps:

1. Clone the repository:
    ```sh
    git clone https://github.com/hoangkhoachau/socketClient.git
    cd socketClient
    ```

2. Build the project:
    ```sh
    make
    ```

3. Run the client:
    ```sh
    ./client [url1] [url2]...
    ```

## Usage
Provide URLs as command-line arguments to download content from those URLs. Example:
```sh
./client http://example.com/file1 http://example.com/folder/
```

## Contributing
If you would like to contribute to this project, please fork the repository and submit a pull request.
