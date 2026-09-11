#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include "ssh_client.hpp"
#include "router_check.hpp"
#include <libssh2.h>
#include <algorithm>
#include <chrono>
#include <memory>

namespace ax {
namespace {
struct Runtime {
    Runtime(){WSADATA data{};need(WSAStartup(MAKEWORD(2,2),&data)==0,"无法启动网络连接。");
        if(libssh2_init(0)!=0){WSACleanup();throw Error("无法初始化 SSH。");}}
    ~Runtime(){libssh2_exit();WSACleanup();}
};
void runtime(){static Runtime initialized;}
struct Connection {
    SOCKET socket=INVALID_SOCKET;LIBSSH2_SESSION* session=nullptr;LIBSSH2_CHANNEL* channel=nullptr;
    ~Connection(){
        // Closing the socket makes cleanup non-blocking even if the peer disappeared.
        if(socket!=INVALID_SOCKET){shutdown(socket,SD_BOTH);closesocket(socket);}
        if(channel)libssh2_channel_free(channel);if(session)libssh2_session_free(session);
    }
};
using Clock=std::chrono::steady_clock;
void check(const std::atomic_bool& cancel,Clock::time_point end){need(!cancel,"已取消检测。");need(Clock::now()<end,"连接超时，请检查路由器地址、SSH 端口和网络。");}
void waitSocket(Connection& c,const std::atomic_bool& cancel,Clock::time_point end,bool connecting=false){
    check(cancel,end);fd_set rd,wr,err;FD_ZERO(&rd);FD_ZERO(&wr);FD_ZERO(&err);FD_SET(c.socket,&err);
    auto directions=c.session?libssh2_session_block_directions(c.session):0;
    if(!connecting&&(directions&LIBSSH2_SESSION_BLOCK_INBOUND))FD_SET(c.socket,&rd);
    if(connecting||(directions&LIBSSH2_SESSION_BLOCK_OUTBOUND)||!directions)FD_SET(c.socket,&wr);
    timeval timeout{0,100000};int result=select(0,&rd,&wr,&err,&timeout);
    need(result!=SOCKET_ERROR&&!FD_ISSET(c.socket,&err),"网络连接已中断。");
}
template<class F> int retry(Connection& c,F fn,const std::atomic_bool& cancel,Clock::time_point end){
    for(;;){check(cancel,end);int result=fn();if(result!=LIBSSH2_ERROR_EAGAIN)return result;waitSocket(c,cancel,end);}
}
}
std::vector<Gateway> localGateways(){
    runtime();PMIB_IPFORWARD_TABLE2 raw=nullptr;need(GetIpForwardTable2(AF_INET,&raw)==NO_ERROR,"无法读取当前网络网关，请手动填写地址。");
    std::unique_ptr<MIB_IPFORWARD_TABLE2,decltype(&FreeMibTable)> table(raw,FreeMibTable);std::vector<Gateway> gateways;
    for(ULONG i=0;i<raw->NumEntries;i++){
        const auto& route=raw->Table[i];if(route.DestinationPrefix.PrefixLength||route.NextHop.Ipv4.sin_addr.s_addr==0)continue;
        MIB_IF_ROW2 network{};network.InterfaceLuid=route.InterfaceLuid;if(GetIfEntry2(&network)!=NO_ERROR||network.OperStatus!=IfOperStatusUp)continue;
        // LAN router access should prefer Ethernet / Wi-Fi gateways over VPN tunnels.
        if(network.Type!=IF_TYPE_ETHERNET_CSMACD&&network.Type!=IF_TYPE_IEEE80211)continue;
        MIB_IPINTERFACE_ROW ip{};InitializeIpInterfaceEntry(&ip);ip.Family=AF_INET;ip.InterfaceLuid=route.InterfaceLuid;
        if(GetIpInterfaceEntry(&ip)!=NO_ERROR)continue;
        char address[INET_ADDRSTRLEN]{};if(!InetNtopA(AF_INET,&route.NextHop.Ipv4.sin_addr,address,sizeof(address)))continue;
        gateways.push_back({address,utf8(network.Alias),uint64_t(route.Metric)+ip.Metric});
    }
    std::stable_sort(gateways.begin(),gateways.end(),[](const auto& a,const auto& b){return a.metric<b.metric;});
    std::set<std::string> seen;gateways.erase(std::remove_if(gateways.begin(),gateways.end(),[&](const auto& g){return !seen.insert(g.address).second;}),gateways.end());return gateways;
}
Bytes queryRouter(const SshOptions& o,const std::atomic_bool& cancel){
    runtime();need(!o.address.empty()&&o.address.size()<128&&o.port,"请填写有效的路由器 IP 地址和端口。");
    need(!o.username.empty()&&o.username.size()<=128&&o.username.find('\0')==o.username.npos,"请填写 SSH 用户名。");need(o.password.size()<=4096,"SSH 密码过长。");
    Connection c;sockaddr_storage address{};int length=0;
    auto* v4=reinterpret_cast<sockaddr_in*>(&address);auto* v6=reinterpret_cast<sockaddr_in6*>(&address);
    if(InetPtonA(AF_INET,o.address.c_str(),&v4->sin_addr)==1){v4->sin_family=AF_INET;v4->sin_port=htons(o.port);length=sizeof(*v4);}
    else if(InetPtonA(AF_INET6,o.address.c_str(),&v6->sin6_addr)==1){v6->sin6_family=AF_INET6;v6->sin6_port=htons(o.port);length=sizeof(*v6);}
    else throw Error("请填写路由器的 IP 地址，例如 192.168.6.1；不要包含 http:// 或路径。");
    auto end=Clock::now()+std::chrono::seconds(15);check(cancel,end);
    c.socket=socket(address.ss_family,SOCK_STREAM,IPPROTO_TCP);need(c.socket!=INVALID_SOCKET,"无法建立网络连接。");u_long nonblocking=1;
    need(ioctlsocket(c.socket,FIONBIO,&nonblocking)==0,"无法设置连接状态。");
    int status=connect(c.socket,reinterpret_cast<sockaddr*>(&address),length);
    if(status==SOCKET_ERROR){need(WSAGetLastError()==WSAEWOULDBLOCK,"无法连接这个地址，请检查 SSH 是否已开启。");
        for(;;){waitSocket(c,cancel,end,true);fd_set writable;FD_ZERO(&writable);FD_SET(c.socket,&writable);timeval zero{};
            int ready=select(0,nullptr,&writable,nullptr,&zero);if(ready<=0)continue;int error=0,n=sizeof(error);need(getsockopt(c.socket,SOL_SOCKET,SO_ERROR,reinterpret_cast<char*>(&error),&n)==0&&error==0,"SSH 连接失败，请检查端口。");break;}}
    c.session=libssh2_session_init();need(c.session,"无法建立 SSH 会话。");libssh2_session_set_blocking(c.session,0);
    status=retry(c,[&]{return libssh2_session_handshake(c.session,c.socket);},cancel,end);
    need(status==0,"SSH 握手失败。请检查 SSH 服务；当前支持 RSA / ECDSA，不支持仅启用 Ed25519 的服务器。");
    size_t keySize=0;int keyType=0;const char* key=libssh2_session_hostkey(c.session,&keySize,&keyType);need(key&&keySize,"无法读取 SSH 设备指纹。");
    auto fingerprint="SHA256:"+base64Encode(hash(Bytes(key,keySize)));while(fingerprint.back()=='=')fingerprint.pop_back();
    // Trust is established before any password is sent, and checked on each connection.
    if(o.fingerprint!=fingerprint)throw HostKeyRequired(fingerprint,!o.fingerprint.empty());
    end=Clock::now()+std::chrono::seconds(20);
    status=retry(c,[&]{return libssh2_userauth_password_ex(c.session,o.username.data(),unsigned(o.username.size()),o.password.data(),unsigned(o.password.size()),nullptr);},cancel,end);
    need(status==0,"SSH 登录失败，请检查用户名和密码，并确认路由器允许密码登录。");
    Bytes framed="KWRTSTUDIO-PROBE-1\n";end=Clock::now()+std::chrono::seconds(30);
    for(const auto& [name,command]:routerProbeCommands()){
        for(;;){check(cancel,end);c.channel=libssh2_channel_open_session(c.session);if(c.channel)break;
            need(libssh2_session_last_errno(c.session)==LIBSSH2_ERROR_EAGAIN,"无法打开 SSH 查询通道。");waitSocket(c,cancel,end);}
        status=retry(c,[&]{return libssh2_channel_exec(c.channel,command.c_str());},cancel,end);
        need(status==0,"路由器未接受检测请求。");Bytes output;size_t errors=0;
        while(!libssh2_channel_eof(c.channel)){
            check(cancel,end);bool received=false;
            for(int stream=0;stream<2;stream++){char data[16384];auto n=libssh2_channel_read_ex(c.channel,stream,data,sizeof(data));
                need(n>=0||n==LIBSSH2_ERROR_EAGAIN,"读取检测结果时连接中断。");if(n>0){received=true;if(stream==0)output.append(data,size_t(n));else errors+=size_t(n);
                    need(output.size()<=1048576&&errors<=65536,"检测返回的数据过大。");}}
            if(!received)waitSocket(c,cancel,end);
        }
        status=retry(c,[&]{return libssh2_channel_close(c.channel);},cancel,end);need(status==0,"检测通道关闭异常。");
        need(libssh2_channel_get_exit_status(c.channel)==0,"路由器未能完成信息读取："+name);
        status=retry(c,[&]{return libssh2_channel_free(c.channel);},cancel,end);need(status==0,"检测通道释放异常。");c.channel=nullptr;
        framed+="@"+name+"\n"+base64Encode(output)+"\n";need(framed.size()<=3*1024*1024,"检测返回的数据过大。");
    }
    return framed+"KWRTSTUDIO-END\n";
}
}
