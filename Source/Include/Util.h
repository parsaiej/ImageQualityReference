#ifndef UTIL_H
#define UTIL_H

namespace ICR
{
    // ---------------------------

    enum WindowMode
    {
        Windowed,
        BorderlessFullscreen,
        ExclusiveFullscreen
    };

    enum SwapEffect
    {
        FlipSequential,
        FlipDiscard
    };

    enum UpdateFlags : uint32_t
    {
        None              = 0,
        Window            = 1 << 0,
        SwapChainRecreate = 1 << 1,
        SwapChainResize   = 1 << 2,
        GraphicsRuntime   = 1 << 3
    };

    // ---------------------------

    class StopWatch
    {
    public:

        StopWatch();

        void Read(float& value);

    private:

        std::chrono::time_point<std::chrono::steady_clock> mPrevTime;
    };

    // ---------------------------

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

    // ---------------------------

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

    // ---------------------------

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

    // ---------------------------

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

    // ---------------------------

    inline std::string HrToString(HRESULT hr)
    {
        char gstr[64] = {};
        sprintf_s(gstr, "HRESULT of 0x%08X", static_cast<UINT>(hr));
        return std::string(gstr);
    }

    class HrException : public std::runtime_error
    {
    public:

        HrException(HRESULT hr) : std::runtime_error(HrToString(hr)), m_hr(hr) {}
        HRESULT Error() const { return m_hr; }

    private:

        const HRESULT m_hr;
    };

    inline void ThrowIfFailed(HRESULT hr)
    {
        if (FAILED(hr))
        {
            throw HrException(hr);
        }
    }

    // ---------------------------

    inline std::string FromWideStr(std::wstring wstr)
    {
        // Determine the size of the resulting string
        int size = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);

        if (size == 0)
            throw std::runtime_error("Failed to convert wide string to string.");

        // Allocate a buffer for the resulting string
        std::string str(size - 1, '\0'); // size - 1 because size includes the null terminator
        WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &str[0], size, nullptr, nullptr);

        return str;
    }

    template <typename T>
    inline bool EnumDropdown(const char* name, int* selectedEnumIndex)
    {
        constexpr auto enumNames = magic_enum::enum_names<T>();

        std::vector<const char*> enumNameStrings(enumNames.size());

        for (uint32_t enumIndex = 0U; enumIndex < enumNames.size(); enumIndex++)
            enumNameStrings[enumIndex] = enumNames[enumIndex].data();

        return ImGui::Combo(name, selectedEnumIndex, enumNameStrings.data(), static_cast<int>(enumNameStrings.size()));
    }

    inline bool StringListDropdown(const char* name, const std::vector<std::string>& strings, int& selectedIndex)
    {
        if (strings.empty())
            return false;

        bool modified = false;

        if (ImGui::BeginCombo(name, strings[selectedIndex].c_str()))
        {
            for (int i = 0; i < static_cast<int>(strings.size()); i++)
            {
                if (ImGui::Selectable(strings[i].c_str(), selectedIndex == i))
                {
                    selectedIndex = i;
                    modified      = true;
                }

                if (selectedIndex == i)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        return modified;
    }

    inline bool StringListDropdown(const char* name, const char* const* cstrings, size_t size, int& selectedIndex)
    {
        std::vector<std::string> strings;

        for (uint32_t i = 0; i < size; i++)
            strings.push_back(cstrings[i]);

        return StringListDropdown(name, strings, selectedIndex);
    }

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

    // ---------------------------

    bool ReadFileBytes(const std::string& filename, std::vector<uint8_t>& data);

    // Query URL and return result in a string (empty it failed).
    std::string QueryURL(const std::string& url);

    // Compile GLSL to SPIR-V using glslang (empty if failed).
    std::vector<uint32_t> CompileGLSLToSPIRV(const char** sources, int sourceCount, EShLanguage stage);

    // Cross compiles a SPIR-V module to DXIL.
    bool CrossCompileSPIRVToDXIL(const std::string& entryPoint, const std::vector<uint32_t>& spirv, std::vector<uint8_t>& dxil);

} // namespace ICR

#endif
