//--- mcast/mcast_recv.cpp ------------------------------------*- C++ -*-==//
//
//               Bellport Low Latency Trading Infrastructure.
//
// Copyright MayStreet 2015 - all rights reserved
//
// $Id: e8ca96d89b50f9438080d85348926de7cad36f7f $
//===--------------------------------------------------------------------===//

// mcast_recv.cpp : Defines the entry point for the console application.
//

#ifdef _WINDOWS
#include <Windows.h>
#include <WinSock2.h>
#include "Mswsock.h"
#include <ws2tcpip.h>
#else
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <errno.h>
typedef int SOCKET;
#define INVALID_SOCKET -1
#define SOCKET_ERROR   -1
#define SO_TIMESTAMPING 37
#endif

// This defined somewhere in the original mcast_recv
#define HAVE_SO_REUSEPORT

#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdio.h>
#include <algorithm>
#include <iostream>
#include <sstream>
#include <cstdlib>
#include <vector>
#include <string>

// bytes received during the program
uint32_t g_bytes_received;
// packets received during the program
uint32_t g_packets_received;
// used to store the command line arguments
std::vector<std::string> g_program_arguments;
// The group:port[:interface] portion of the arguments
std::string g_address;

// This will be called at the end of the program
void ex_program(int sig) {
  fprintf(stderr, "Received %d bytes\n", g_bytes_received);
  fprintf(stderr, "Received %d packets\n", g_packets_received);
  exit(EXIT_SUCCESS);
}

// If the user specified incorrect arguments or needs help, this function
// will be called
void Usage() {
  std::cerr << "USAGE: mcast_recv <flags> group:port[:interface]" << std::endl;
  std::cerr << std::endl;
  std::cerr << "Receive data from a given multicast group" << std::endl;
  std::cerr << std::endl;
  std::cerr << "  group:port[:interface] The multicast group and port with an optional interface IP Address" << std::endl;
  std::cerr << "  -q                     Quiet, suppress output the number of bytes received from each packet" << std::endl;
  std::cerr << "  -t                     Turn on hardware stamping" << std::endl;
  std::cerr << "  -h                     Display this help and exit" << std::endl;
}

int LastError() {
#ifdef _WINDOWS
  return GetLastError();
#else
  return errno;
#endif
}

bool IsFlagSet(const std::string& arg);
bool OpenMulticastSocket(SOCKET* socket);

struct IPMReq {
  IPMReq(unsigned long group_address, unsigned long interface_address) {
#ifdef _WINDOWS
    mreq_.imr_multiaddr.S_un.S_addr = group_address;
    mreq_.imr_interface.S_un.S_addr = interface_address;
#else
    mreq_.imr_multiaddr.s_addr = group_address;
    mreq_.imr_interface.s_addr = interface_address;
#endif
  }

  const ip_mreq& mreq() const { return mreq_; }
private:
  ip_mreq mreq_;
};

struct Socket {
  Socket()
    : socket_(INVALID_SOCKET)
  { }

  ~Socket() {
    Close();
  }

  bool Open();
  bool IsOpen() const;
  template<class Value>
  bool AddOption(int level, int optname, const Value& value);
  void Close();
  bool Bind(unsigned long protocol, uint16_t port);
  bool AddMember(const IPMReq& mreq);
  operator const void*() const { return IsOpen() ? static_cast<const void*>(this) : NULL; }
  bool Receive(char* data, uint32_t max_length, uint32_t flags, int32_t* received);
private:
  SOCKET socket_;
};

bool Socket::Open() {
  OpenMulticastSocket(&socket_);
  return IsOpen();
}

bool Socket::IsOpen() const {
  return socket_ != INVALID_SOCKET;
}

template<class Value>
bool Socket::AddOption(int level, int optname, const Value& value) {
  return setsockopt(socket_, level, optname, reinterpret_cast<const char*>(&value), sizeof value) != SOCKET_ERROR;
}

void Socket::Close() {
#ifdef _WINDOWS
  closesocket(socket_);
#else
  close(socket_);
#endif
}

bool Socket::Bind(unsigned long protocol, uint16_t port) {
  in_addr temp;
  memset(reinterpret_cast<char*>(&temp), 0, sizeof temp);
  temp.s_addr = htonl(protocol);

  struct sockaddr_in serv_addr;
  memset(reinterpret_cast<char*>(&serv_addr), 0, sizeof serv_addr);
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons(port);
#ifdef _WINDOWS
  serv_addr.sin_addr.S_un.S_addr = temp.S_un.S_addr;
#else
  serv_addr.sin_addr = temp;
#endif
  int result = bind(socket_, reinterpret_cast<sockaddr*>(&serv_addr), sizeof serv_addr);
  return result != SOCKET_ERROR;
}

bool Socket::Receive(char* data, uint32_t max_length, uint32_t flags, int32_t* received) {
  *received = 0;
  int result = recv(socket_, data, max_length, flags);
  if (result > 0) {
    *received = result;
  }
  else if (result == 0) {
    return false;
  } else {
    *received = 0;
#ifdef _WINDOWS
    return (LastError() == 10060 || // WSAETIMEDOUT
            LastError() == 10035);  // WSAEWOULDBLOCK
#else
    return (LastError() == EAGAIN || LastError() == EWOULDBLOCK || LastError() == EINTR);
#endif
  }
  return true;
}

bool Socket::AddMember(const IPMReq& ipmreq) {
  return AddOption(IPPROTO_IP, IP_ADD_MEMBERSHIP, ipmreq.mreq());
}

template<class T>
struct optional {
  optional(const T& val) : t(new (std::nothrow) T(val)) { }
  optional() : t(NULL) { }
  T value() const { return *t; }
  T value_or(const T& alt) const { return *this ? *t : alt; }

  operator const void*() const {
    return t;
  }

  const T& operator*() const { return *t; }
        T* operator*() { return *t; }
        T* operator->() { return &t; }
  const T* operator->() const { return &t; }

  ~optional() { delete t; }
private:
    T* t;
};

template<class T>
optional<T> make_optional(const T& value) {
  return optional<T>(value);
}

template<class T>
optional<T> make_optional() {
  return optional<T>();
}

namespace detail {
// How to split a string: http://stackoverflow.com/a/236803/701092
std::vector<std::string>& Split(const std::string& s, char delim, std::vector<std::string>& elems) {
  std::stringstream ss(s);
  std::string item;
  while (std::getline(ss, item, delim)) {
      elems.push_back(item);
  }
  return elems;
}
}  // detail

std::vector<std::string> Split(const std::string& s, char delim) {
  std::vector<std::string> elems;
  detail::Split(s, delim, elems);
  return elems;
}

bool NotAFlag(const std::string& str) {
  return str.at(0) != '-';
}

template<class T>
void Init(const T& begin, const T& end) {
  g_program_arguments.assign(begin, end);
  g_address = *std::find_if(begin, end, NotAFlag);
}

std::vector<std::string>::iterator FindFlag(const std::string& arg) {
  return std::find(g_program_arguments.begin(), g_program_arguments.end(), arg);
}

bool IsFlagSet(const std::string& arg) {
  return FindFlag(arg) != g_program_arguments.end();
}

optional<std::string> GetSingleArgOption(const std::string& flag) {
  typedef std::vector<std::string>::iterator Iter;
  Iter pos = FindFlag(flag);
  Iter next = pos + 1;
  if (pos != g_program_arguments.end() && next != g_program_arguments.end()) {
    if (NotAFlag(next->substr(0, 2))) {
        return *next;
    }
  }
  return make_optional<std::string>();
}

bool OpenMulticastSocket(SOCKET* socket) {
  *socket = ::socket(AF_INET, SOCK_DGRAM, 0);
  return socket != 0;
}

#ifdef _WINDOWS
void InitWinSock() {
  WSADATA wsadata;
  int err = ::WSAStartup(MAKEWORD(1, 1), &wsadata);
  if (err != 0) {
    printf("WSAStartup failed with error: %d\n", err);
  }
}
#endif

unsigned long GetInterface(const std::vector<std::string>& tokens) {
  if (tokens.size() >= 3) {
    return inet_addr(tokens[2].c_str());
  }
  return INADDR_ANY; // INADDR_ANY == 0
}

int main(int argc, char* argv[]) {
  if (argc < 2) {
    Usage();
    exit(EXIT_FAILURE);
  }

  signal(SIGINT, ex_program);
  Init(argv + 1, argv + argc);

  bool result;
  bool suppress_output = IsFlagSet("-q");
  bool timestamp_mode = IsFlagSet("-t");
  bool need_help = IsFlagSet("-h");

  if (need_help)
  {
    Usage();
    return EXIT_SUCCESS;
  }

  std::vector<std::string> tokens = Split(g_address, ':');
  if (tokens.size() < 2) {
    Usage();
    return EXIT_FAILURE;
  }

#ifdef _WINDOWS
  InitWinSock();
#endif

  Socket socket;
  socket.Open();

  if (!socket) {
    std::cerr << "Error opening socket: INVALID_SOCKET\n";
    std::cerr << LastError() << '\n';
    exit(EXIT_FAILURE);
  } 
   
  unsigned long group_address = inet_addr(tokens[0].c_str());
  unsigned long interface_address = GetInterface(tokens);

  if (!socket.AddOption(SOL_SOCKET, SO_REUSEADDR, 1)) {
    std::cerr << "Error adding socket option: SO_REUSEADDR\n";
    exit(EXIT_FAILURE);
  }


#ifdef HAVE_SO_REUSEPORT
  result = socket.AddOption(SOL_SOCKET, SO_REUSEPORT, 1);
#else
  result = true;
#endif

  if (!result) {
    std::cerr << "Error adding socket option: SO_REUSEPORT\n";
    std::cerr << LastError() << '\n';
    exit(EXIT_FAILURE);
  }

  if (timestamp_mode) { 
    result = socket.AddOption(SOL_SOCKET, SO_TIMESTAMPING, 1);
  }

  if (!result) {
    std::cerr << "Error adding socket option: SO_TIMESTAMPING\n";
    std::cerr << LastError() << '\n';
    std::cerr << "Continuing...\n";
  }

  uint16_t portno = atoi(tokens[1].c_str());
  if (!socket.Bind(INADDR_ANY, portno)) {
    std::cerr << "Error on binding\n";
    exit(EXIT_FAILURE);
  }

  IPMReq mreq(group_address, interface_address);
  if (!socket.AddMember(mreq)) {
    std::cerr << "mcast_recv: ERROR could not bind to create multicast with specified parameters" << std::endl;
    std::cerr << LastError() << '\n';
    Usage();
    exit(EXIT_FAILURE);
  }

  std::cout << "Receiving Data...\n" << std::endl;

  const std::size_t kDataSize = 1500;
  char data[kDataSize] = {};

  for (;;) {
    int32_t received(0);
    if (!socket.Receive(&data[0], kDataSize, 0, &received)) {
      break;
    }
    g_bytes_received += received;

    if (received > 0) {
      ++g_packets_received;
    }

    if (!suppress_output) {
      std::cout << "Got: " << received << " bytes\n";
    }

    std::cout << "Message: " << data << '\n';
  }
  return EXIT_SUCCESS;
}