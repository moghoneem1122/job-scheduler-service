#pragma once
#include "crow.h"
#include <string>

inline crow::json::wvalue success(const std::string& message,
 crow::json::wvalue data = crow::json::wvalue())
{
    crow::json::wvalue res;
    res["status"] = "success";
    res["message"] = message;
    res["data"] = std::move(data);
    return res;
}

inline crow::json::wvalue error(const std::string& message)
{
    crow::json::wvalue res;
    res["status"] = "error";
    res["message"] = message;
    return res;
}