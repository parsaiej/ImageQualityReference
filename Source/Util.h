#ifndef UTIL_H
#define UTIL_H

class StopWatch
{
public:

    StopWatch();

    void Read(float& value);

private:

    std::chrono::time_point<std::chrono::steady_clock> mPrevTime;
};

class ScrollingBuffer
{
public:

    ScrollingBuffer(int sizeMax = 8000);

    void AddPoint(float x, float y);

    void Erase();

    int              mSizeMax;
    int              mOffset;
    ImVector<ImVec2> mData;
};

class MovingAverage
{
public:

    MovingAverage(int windowSize);
    ~MovingAverage();

    void  AddValue(float value);
    float GetAverage() const;

private:

    int    mWindowSize;
    int    mCount;
    float  mSum;
    int    mIndex;
    float* mValues;
};

inline void SetDebugName(ID3D12Object* obj, const wchar_t* name)
{
    if (obj)
        obj->SetName(name);
}

inline void SetDebugName(IDXGIObject* obj, const wchar_t* name)
{
    if (obj)
        obj->SetPrivateData(WKPDID_D3DDebugObjectNameW, static_cast<UINT>(wcslen(name) * sizeof(wchar_t)), name);
}

struct XMINT2Cmp
{
    bool operator()(const DirectX::XMINT2& lhs, const DirectX::XMINT2& rhs) const
    {
        if (lhs.x != rhs.x)
            return lhs.x < rhs.x;
        return lhs.y < rhs.y;
    }
};

struct RefreshRateCmp
{
    bool operator()(const DXGI_RATIONAL& lhs, const DXGI_RATIONAL& rhs) const
    {
        return lhs.Numerator * rhs.Denominator < rhs.Numerator * lhs.Denominator;
    }
};

class D3DMemoryLeakReport
{
public:

    ~D3DMemoryLeakReport()
    {
        IDXGIDebug1* dxgiDebug;
        if (FAILED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&dxgiDebug))))
            return;

        dxgiDebug->ReportLiveObjects(DXGI_DEBUG_ALL, static_cast<DXGI_DEBUG_RLO_FLAGS>(DXGI_DEBUG_RLO_IGNORE_INTERNAL | DXGI_DEBUG_RLO_DETAIL));
        dxgiDebug->Release();
    }
};

#endif
