#pragma once

#include <string>

void Init(std::string host = "127.0.0.1", int port = 8332, std::string username = "", std::string password = "");
std::string ParseTx(std::string hexTx, int blockHeight, std::string vinsJson = "[]");