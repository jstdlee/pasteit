#include "net/http_client.hpp"

#if defined(PASTEIT_HAS_CURL)
#include <curl/curl.h>
#elif defined(_WIN32)
#include "platform/windows/windows_strings.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <cstdint>
#else
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iterator>
#include <netdb.h>
#include <optional>
#include <sstream>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

extern char** environ;
#endif

namespace pasteit {
namespace {
#if defined(PASTEIT_HAS_CURL)
std::size_t append_body(void* ptr,std::size_t size,std::size_t count,void* target){
    const auto bytes=size*count;static_cast<std::string*>(target)->append(static_cast<const char*>(ptr),bytes);return bytes;
}
struct CappedBody { std::string* body; std::size_t cap; bool truncated=false; };
// Stops the transfer once the cap is reached; the partial body is kept.
std::size_t append_capped(void* ptr,std::size_t size,std::size_t count,void* target){
    auto& capped=*static_cast<CappedBody*>(target);const auto bytes=size*count;
    const auto room=capped.cap>capped.body->size()?capped.cap-capped.body->size():0;
    capped.body->append(static_cast<const char*>(ptr),std::min(bytes,room));
    if(bytes>room){capped.truncated=true;return 0;}
    return bytes;
}
class CurlTransport final:public HttpTransport{
public: HttpResponse post_json(const HttpRequest& request) override {
    HttpResponse response; CURL* curl=curl_easy_init();
    if(!curl){response.transport_error="libcurl initialization failed";return response;}
    curl_slist* headers=nullptr; headers=curl_slist_append(headers,"Content-Type: application/json");
    for(const auto& [name,value]:request.headers){const auto line=name+": "+value;headers=curl_slist_append(headers,line.c_str());}
    curl_easy_setopt(curl,CURLOPT_URL,request.url.c_str());curl_easy_setopt(curl,CURLOPT_POST,1L);
    curl_easy_setopt(curl,CURLOPT_HTTPHEADER,headers);curl_easy_setopt(curl,CURLOPT_POSTFIELDS,request.body.c_str());
    curl_easy_setopt(curl,CURLOPT_POSTFIELDSIZE,static_cast<long>(request.body.size()));
    curl_easy_setopt(curl,CURLOPT_TIMEOUT_MS,static_cast<long>(request.timeout.count()));
    curl_easy_setopt(curl,CURLOPT_WRITEFUNCTION,append_body);curl_easy_setopt(curl,CURLOPT_WRITEDATA,&response.body);
    const auto code=curl_easy_perform(curl);long status=0;curl_easy_getinfo(curl,CURLINFO_RESPONSE_CODE,&status);response.status=static_cast<int>(status);
    if(code!=CURLE_OK)response.transport_error=curl_easy_strerror(code);
    curl_slist_free_all(headers);curl_easy_cleanup(curl);return response;
  }
  HttpResponse get_json(const HttpRequest& request) override {
    HttpResponse response; CURL* curl=curl_easy_init();
    if(!curl){response.transport_error="libcurl initialization failed";return response;}
    curl_slist* headers=nullptr;
    for(const auto& [name,value]:request.headers){const auto line=name+": "+value;headers=curl_slist_append(headers,line.c_str());}
    curl_easy_setopt(curl,CURLOPT_URL,request.url.c_str());curl_easy_setopt(curl,CURLOPT_HTTPGET,1L);
    curl_easy_setopt(curl,CURLOPT_HTTPHEADER,headers);curl_easy_setopt(curl,CURLOPT_TIMEOUT_MS,static_cast<long>(request.timeout.count()));
    if(request.follow_redirects){
        curl_easy_setopt(curl,CURLOPT_FOLLOWLOCATION,1L);curl_easy_setopt(curl,CURLOPT_MAXREDIRS,5L);
        curl_easy_setopt(curl,CURLOPT_PROTOCOLS_STR,"http,https");curl_easy_setopt(curl,CURLOPT_REDIR_PROTOCOLS_STR,"http,https");
    }
    CappedBody capped{&response.body,request.max_body_bytes};
    if(request.max_body_bytes>0){curl_easy_setopt(curl,CURLOPT_WRITEFUNCTION,append_capped);curl_easy_setopt(curl,CURLOPT_WRITEDATA,&capped);}
    else{curl_easy_setopt(curl,CURLOPT_WRITEFUNCTION,append_body);curl_easy_setopt(curl,CURLOPT_WRITEDATA,&response.body);}
    const auto code=curl_easy_perform(curl);long status=0;curl_easy_getinfo(curl,CURLINFO_RESPONSE_CODE,&status);response.status=static_cast<int>(status);
    char* content_type=nullptr;if(curl_easy_getinfo(curl,CURLINFO_CONTENT_TYPE,&content_type)==CURLE_OK&&content_type)response.content_type=content_type;
    response.truncated=capped.truncated;
    if(code!=CURLE_OK&&!(code==CURLE_WRITE_ERROR&&capped.truncated))response.transport_error=curl_easy_strerror(code);
    curl_slist_free_all(headers);curl_easy_cleanup(curl);return response;
  }
};
#elif defined(_WIN32)
struct WinHttpHandle {
    HINTERNET value = nullptr;
    WinHttpHandle() = default;
    explicit WinHttpHandle(HINTERNET handle) : value(handle) {}
    ~WinHttpHandle() {
        if (value != nullptr) {
            WinHttpCloseHandle(value);
        }
    }
    WinHttpHandle(const WinHttpHandle&) = delete;
    WinHttpHandle& operator=(const WinHttpHandle&) = delete;
    explicit operator bool() const { return value != nullptr; }
};

std::string last_winhttp_error(std::string_view action) {
    return std::string{action} + " failed with Windows error " + std::to_string(GetLastError());
}

std::wstring request_path(const URL_COMPONENTS& components) {
    std::wstring path;
    if (components.dwUrlPathLength > 0 && components.lpszUrlPath != nullptr) {
        path.append(components.lpszUrlPath, components.dwUrlPathLength);
    }
    if (path.empty()) {
        path = L"/";
    }
    if (components.dwExtraInfoLength > 0 && components.lpszExtraInfo != nullptr) {
        path.append(components.lpszExtraInfo, components.dwExtraInfoLength);
    }
    return path;
}

HttpResponse winhttp_json_request(const HttpRequest& request, const wchar_t* method) {
    HttpResponse response;
    const auto url = utf8_to_wide(request.url);
    if (url.empty()) {
        response.transport_error = "URL is not valid UTF-8";
        return response;
    }

    URL_COMPONENTS components{};
    components.dwStructSize = sizeof(components);
    components.dwSchemeLength = static_cast<DWORD>(-1);
    components.dwHostNameLength = static_cast<DWORD>(-1);
    components.dwUrlPathLength = static_cast<DWORD>(-1);
    components.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(url.c_str(), static_cast<DWORD>(url.size()), 0, &components) ||
        components.lpszHostName == nullptr || components.dwHostNameLength == 0) {
        response.transport_error = "HTTP endpoint URL is invalid";
        return response;
    }

    const std::wstring host{components.lpszHostName, components.dwHostNameLength};
    const auto path = request_path(components);
    const DWORD flags = components.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
    const int timeout = static_cast<int>(std::max<std::int64_t>(1, request.timeout.count()));

    WinHttpHandle session{WinHttpOpen(L"PasteIt/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                      WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0)};
    if (!session) {
        response.transport_error = last_winhttp_error("WinHttpOpen");
        return response;
    }
    WinHttpSetTimeouts(session.value, timeout, timeout, timeout, timeout);

    WinHttpHandle connection{WinHttpConnect(session.value, host.c_str(), components.nPort, 0)};
    if (!connection) {
        response.transport_error = last_winhttp_error("WinHttpConnect");
        return response;
    }

    WinHttpHandle http_request{WinHttpOpenRequest(connection.value, method, path.c_str(), nullptr,
                                                  WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags)};
    if (!http_request) {
        response.transport_error = last_winhttp_error("WinHttpOpenRequest");
        return response;
    }

    std::wstring headers;
    if (request.body.empty()) {
        headers += L"Accept: application/json\r\n";
    } else {
        headers += L"Content-Type: application/json\r\nAccept: application/json\r\n";
    }
    for (const auto& [name, value] : request.headers) {
        headers += utf8_to_wide(name + ": " + value);
        headers += L"\r\n";
    }
    if (!headers.empty() &&
        !WinHttpAddRequestHeaders(http_request.value, headers.c_str(), static_cast<DWORD>(headers.size()),
                                  WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE)) {
        response.transport_error = last_winhttp_error("WinHttpAddRequestHeaders");
        return response;
    }

    const DWORD body_size = static_cast<DWORD>(request.body.size());
    void* body = body_size == 0 ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(request.body.data());
    if (!WinHttpSendRequest(http_request.value, WINHTTP_NO_ADDITIONAL_HEADERS, 0, body, body_size,
                            body_size, 0)) {
        response.transport_error = last_winhttp_error("WinHttpSendRequest");
        return response;
    }
    if (!WinHttpReceiveResponse(http_request.value, nullptr)) {
        response.transport_error = last_winhttp_error("WinHttpReceiveResponse");
        return response;
    }

    DWORD status = 0;
    DWORD status_size = sizeof(status);
    if (WinHttpQueryHeaders(http_request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size, WINHTTP_NO_HEADER_INDEX)) {
        response.status = static_cast<int>(status);
    }

    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(http_request.value, &available)) {
            response.transport_error = last_winhttp_error("WinHttpQueryDataAvailable");
            return response;
        }
        if (available == 0) {
            break;
        }
        if (request.max_body_bytes > 0 && response.body.size() >= request.max_body_bytes) {
            response.truncated = true;
            break;
        }
        const auto offset = response.body.size();
        response.body.resize(offset + available);
        DWORD read = 0;
        if (!WinHttpReadData(http_request.value, response.body.data() + offset, available, &read)) {
            response.transport_error = last_winhttp_error("WinHttpReadData");
            return response;
        }
        response.body.resize(offset + read);
    }
    return response;
}

class WinHttpTransport final : public HttpTransport {
public:
    HttpResponse post_json(const HttpRequest& request) override {
        return winhttp_json_request(request, L"POST");
    }

    HttpResponse get_json(const HttpRequest& request) override {
        return winhttp_json_request(request, L"GET");
    }
};
#else
struct PlainUrl{std::string host,port="80",path="/";};

std::string curl_config_quote(std::string_view value) {
    std::string result;
    result.reserve(value.size() + 8);
    for (const char ch : value) {
        if (ch == '\\' || ch == '"') result.push_back('\\');
        if (ch == '\n' || ch == '\r') result.push_back(' ');
        else result.push_back(ch);
    }
    return result;
}

bool write_pipe(int fd, std::string_view value) {
    while (!value.empty()) {
        const auto count = ::write(fd, value.data(), value.size());
        if (count <= 0) return false;
        value.remove_prefix(static_cast<std::size_t>(count));
    }
    return true;
}

HttpResponse post_json_with_curl_cli(const HttpRequest& request, std::string_view method = "POST") {
    HttpResponse response;
    char body_template[] = "/tmp/pasteit-http-body-XXXXXX";
    const int body_fd = ::mkstemp(body_template);
    if (body_fd < 0) {
        response.transport_error = "could not create temporary HTTP request body";
        return response;
    }
    const std::string body_path = body_template;
    bool body_ok = write_pipe(body_fd, request.body);
    ::close(body_fd);
    if (!body_ok) {
        ::unlink(body_path.c_str());
        response.transport_error = "could not write temporary HTTP request body";
        return response;
    }

    int input_pipe[2]{-1, -1};
    int output_pipe[2]{-1, -1};
    if (::pipe(input_pipe) != 0 || ::pipe(output_pipe) != 0) {
        for (const int fd : input_pipe) if (fd >= 0) ::close(fd);
        for (const int fd : output_pipe) if (fd >= 0) ::close(fd);
        ::unlink(body_path.c_str());
        response.transport_error = "could not create curl adapter pipes";
        return response;
    }

    const auto timeout_seconds = std::max<std::int64_t>(1, (request.timeout.count() + 999) / 1000);
    std::vector<std::string> arguments{
        "curl", "--silent", "--show-error", "--location", "--max-time", std::to_string(timeout_seconds),
        "--output", body_path, "--write-out", "%{http_code}", "--proto", "=http,https,file", "--config", "-",
        "--proto-redir", "=http,https", request.url,
    };
    std::string config = "request = \"" + std::string{method} + "\"\n";
    for (const auto& [name, value] : request.headers) {
        config += "header = \"" + curl_config_quote(name + ": " + value) + "\"\n";
    }
    if (!request.body.empty()) {
        config += "data-binary = @" + curl_config_quote(body_path) + "\n";
    }

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, input_pipe[0], STDIN_FILENO);
    posix_spawn_file_actions_adddup2(&actions, output_pipe[1], STDOUT_FILENO);
    posix_spawn_file_actions_addclose(&actions, input_pipe[1]);
    posix_spawn_file_actions_addclose(&actions, output_pipe[0]);
    pid_t pid = 0;
    std::vector<char*> argv;
    argv.reserve(arguments.size() + 1);
    for (auto& argument : arguments) argv.push_back(argument.data());
    argv.push_back(nullptr);
    const int spawn_error = posix_spawnp(&pid, argv.front(), &actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    ::close(input_pipe[0]);
    ::close(output_pipe[1]);
    if (spawn_error != 0) {
        ::close(input_pipe[1]);
        ::close(output_pipe[0]);
        ::unlink(body_path.c_str());
        response.transport_error = "HTTPS transport requires libcurl or the curl executable";
        return response;
    }

    const bool request_written = write_pipe(input_pipe[1], config);
    ::close(input_pipe[1]);
    std::string status_text;
    char buffer[64]{};
    for (;;) {
        const auto count = ::read(output_pipe[0], buffer, sizeof(buffer));
        if (count == 0) break;
        if (count < 0) {
            status_text.clear();
            break;
        }
        status_text.append(buffer, static_cast<std::size_t>(count));
    }
    ::close(output_pipe[0]);
    int status = 0;
    (void)::waitpid(pid, &status, 0);
    if (!request_written || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        ::unlink(body_path.c_str());
        response.transport_error = "curl HTTPS request failed";
        return response;
    }
    if (status_text.size() >= 3) response.status = std::atoi(status_text.substr(status_text.size() - 3).c_str());
    std::ifstream body(body_path, std::ios::binary);
    response.body.assign(std::istreambuf_iterator<char>{body}, std::istreambuf_iterator<char>{});
    ::unlink(body_path.c_str());
    if (request.max_body_bytes > 0 && response.body.size() > request.max_body_bytes) {
        response.body.resize(request.max_body_bytes);
        response.truncated = true;
    }
    return response;
}

std::optional<PlainUrl> parse_http(std::string_view url,std::string& error){
 constexpr std::string_view prefix="http://";if(!url.starts_with(prefix)){error="HTTPS requires libcurl in this build";return std::nullopt;}
 std::string rest{url.substr(prefix.size())};const auto slash=rest.find('/');const auto authority=slash==std::string::npos?rest:rest.substr(0,slash);PlainUrl parsed;parsed.path=slash==std::string::npos?"/":rest.substr(slash);const auto colon=authority.rfind(':');if(colon==std::string::npos)parsed.host=authority;else{parsed.host=authority.substr(0,colon);parsed.port=authority.substr(colon+1);}if(parsed.host.empty()||parsed.port.empty()){error="HTTP endpoint host or port is empty";return std::nullopt;}return parsed;
}
bool write_all(int fd,std::string_view value){while(!value.empty()){const auto sent=send(fd,value.data(),value.size(),0);if(sent<=0)return false;value.remove_prefix(static_cast<std::size_t>(sent));}return true;}
HttpResponse socket_json_request(const HttpRequest& request, std::string_view method){
 HttpResponse response;std::string error;const auto url=parse_http(request.url,error);if(!url){response.transport_error=error;return response;}
 addrinfo hints{};hints.ai_family=AF_UNSPEC;hints.ai_socktype=SOCK_STREAM;addrinfo* addresses=nullptr;if(const int code=getaddrinfo(url->host.c_str(),url->port.c_str(),&hints,&addresses);code!=0){response.transport_error=gai_strerror(code);return response;}
 int fd=-1;for(auto* address=addresses;address;address=address->ai_next){fd=socket(address->ai_family,address->ai_socktype,address->ai_protocol);if(fd<0)continue;timeval timeout{static_cast<long>(request.timeout.count()/1000),static_cast<long>((request.timeout.count()%1000)*1000)};setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout));if(connect(fd,address->ai_addr,address->ai_addrlen)==0)break;close(fd);fd=-1;}freeaddrinfo(addresses);if(fd<0){response.transport_error=std::strerror(errno);return response;}
 std::ostringstream message;message<<method<<" "<<url->path<<" HTTP/1.1\r\nHost: "<<url->host<<"\r\nAccept: application/json\r\nConnection: close\r\n";if(!request.body.empty())message<<"Content-Type: application/json\r\nContent-Length: "<<request.body.size()<<"\r\n";for(const auto& [name,value]:request.headers)message<<name<<": "<<value<<"\r\n";message<<"\r\n"<<request.body;const auto wire=message.str();if(!write_all(fd,wire)){response.transport_error=std::strerror(errno);close(fd);return response;}
 std::string raw;char buffer[8192];for(;;){const auto count=recv(fd,buffer,sizeof(buffer),0);if(count==0)break;if(count<0){response.transport_error=std::strerror(errno);close(fd);return response;}raw.append(buffer,static_cast<std::size_t>(count));}close(fd);const auto line=raw.find("\r\n");const auto split=raw.find("\r\n\r\n");if(line==std::string::npos||split==std::string::npos){response.transport_error="Invalid HTTP response";return response;}const auto status_line=raw.substr(0,line);if(status_line.size()>=12)response.status=std::atoi(status_line.substr(9,3).c_str());response.body=raw.substr(split+4);return response;
}
class SocketTransport final:public HttpTransport{public:HttpResponse post_json(const HttpRequest& request)override{if(request.url.starts_with("https://"))return post_json_with_curl_cli(request);return socket_json_request(request,"POST");}HttpResponse get_json(const HttpRequest& request)override{if(request.url.starts_with("https://")||request.follow_redirects)return post_json_with_curl_cli(request,"GET");return socket_json_request(request,"GET");}};
#endif
}
std::shared_ptr<HttpTransport> make_default_http_transport(){
#if defined(PASTEIT_HAS_CURL)
 return std::make_shared<CurlTransport>();
#elif defined(_WIN32)
 return std::make_shared<WinHttpTransport>();
#else
 return std::make_shared<SocketTransport>();
#endif
}
}
