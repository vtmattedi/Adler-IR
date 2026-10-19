struct AcInternalState{
    bool power;
    int temperature;
    bool turbo;
    static const int MAX_TEMP = 30;
    static const int MIN_TEMP = 18;
};

struct AcPhysicalState{
    bool fan;
    bool compressor;
};