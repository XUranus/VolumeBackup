

class SystemApiException : public std::exception {
public:
    // Constructor
    explicit SystemApiException(ErrCodeType errorCode);
    SystemApiException(const char* message, ErrCodeType errorCode);
    const char* what() const noexcept override;
private:
    std::string m_message;
};

bool TruncateCreateFile(const std::string& path, uint64_t size, ErrCodeType& errorCode);

bool IsFileExists(const std::string& path);

uint64_t GetFileSize(const std::string& path);

bool IsDirectoryExists(const std::string& path);

uint8_t* ReadBinaryBuffer(const std::string& filepath, uint64_t length);

bool WriteBinaryBuffer(const std::string& filepath, const uint8_t* buffer, uint64_t length);

bool IsVolumeExists(const std::string& volumePath);

uint64_t ReadVolumeSize(const std::string& volumePath);

uint32_t ProcessorsNum();

#ifdef __linux__
uint64_t ReadSectorSizeLinux(const std::string& devicePath);
#endif