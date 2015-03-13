cmake_minimum_required(VERSION 2.8)

PROJECT(mcast_recv)

if(WIN32)
  #Prevent inclusion of winsock.h in windows.h
  add_definitions(-D_WINSOCKAPI_)
  #add_definitions(/DFD_SETSIZE=256)
  add_definitions(/D_CRT_SECURE_NO_WARNINGS)
endif(WIN32)


ADD_EXECUTABLE(mcast_recv mcast_recv.cpp)

if (WIN32)

  target_link_libraries(mcast_recv
    shlwapi
    ws2_32
    wininet 
    )
ENDIF(WIN32)