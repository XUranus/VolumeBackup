#include <cstdarg>
#include <unistd.h>

#include "Logger.h"

Logger Logger::instance;

Logger& Logger::GetInstance()
{
    return instance;
}

Logger::Logger()
{}

Logger::~Logger()
{}