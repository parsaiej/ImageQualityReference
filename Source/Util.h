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

#endif
