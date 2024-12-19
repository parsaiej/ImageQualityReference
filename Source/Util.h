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

#endif
