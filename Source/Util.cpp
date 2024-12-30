#include <Util.h>

namespace ICR
{

    // StopWatch
    // -----------------------------------------

    StopWatch::StopWatch() { mPrevTime = std::chrono::steady_clock::now(); }

    void StopWatch::Read(float& value)
    {
        auto now  = std::chrono::steady_clock::now();
        value     = std::chrono::duration_cast<std::chrono::microseconds>(now - mPrevTime).count() / 1000000.0f;
        mPrevTime = now;
    }

    // ScrollingBuffer
    // ------------------------------------------

    ScrollingBuffer::ScrollingBuffer(int maxSize)
    {
        this->mSizeMax = maxSize;
        mOffset        = 0;
        mData.reserve(maxSize);
    }

    void ScrollingBuffer::AddPoint(float x, float y)
    {
        if (mData.size() < mSizeMax)
            mData.push_back(ImVec2(x, y));
        else
        {
            mData[mOffset] = ImVec2(x, y);

            mOffset = (mOffset + 1) % mSizeMax;
        }
    }

    void ScrollingBuffer::Erase()
    {
        if (mData.size() > 0)
        {
            mData.shrink(0);
            mOffset = 0;
        }
    }

    // MovingAverage
    // --------------------------------------------

    MovingAverage::MovingAverage(int windowSize) : mWindowSize(windowSize), mCount(0), mSum(0.0f), mIndex(0) { mValues = new float[mWindowSize]; }

    // Destructor to free allocated memory
    MovingAverage::~MovingAverage() { delete[] mValues; }

    // Add a new value to the moving average
    void MovingAverage::AddValue(float value)
    {
        if (mCount == mWindowSize)
            mSum -= mValues[mIndex];
        else
            ++mCount;

        mValues[mIndex] = value;
        mSum += value;

        mIndex = (mIndex + 1) % mWindowSize;
    }

    float MovingAverage::GetAverage() const
    {
        if (mCount == 0)
            return 0.0f;

        return mSum / mCount;
    }

    // Generic functions.
    // --------------------------------------------

    bool ReadFileBytes(const std::string& filename, std::vector<uint8_t>& data)
    {
        std::ifstream file(filename, std::ios::binary | std::ios::ate);

        if (!file)
            return false;

        std::streamsize fileSize = file.tellg();
        file.seekg(0, std::ios::beg);

        data.resize(fileSize);

        if (!file.read(reinterpret_cast<char*>(data.data()), data.size()))
            return false;

        return true;
    }

    class CurlInstance
    {
    public:

        CurlInstance()
        {
            curl_global_init(CURL_GLOBAL_DEFAULT);
            mpCurl = curl_easy_init();
        }

        ~CurlInstance()
        {
            if (mpCurl)
                curl_easy_cleanup(mpCurl);

            curl_global_cleanup();
        }

        inline CURL* Get() { return mpCurl; }

    private:

        CURL* mpCurl;
    };

    static std::unique_ptr<CurlInstance> sCurlInstance = nullptr;

    template <typename T>
    T QueryURL(const std::string& url)
    {
        CURLcode result;

        T data;

        if (!sCurlInstance)
            sCurlInstance = std::make_unique<CurlInstance>();

        auto curl = sCurlInstance->Get();

        if (!curl)
            return T();

        auto CurlWriteCallback = +[](void* contents, size_t size, size_t nmemb, T* userp)
        {
            size_t totalSize = size * nmemb;
            userp->insert(userp->end(), static_cast<uint8_t*>(contents), static_cast<uint8_t*>(contents) + totalSize);
            return totalSize;
        };

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &data);
        result = curl_easy_perform(curl);

        if (result != CURLE_OK)
        {
            spdlog::error("Failed to download shader toy shader: {}", curl_easy_strerror(result));
            return T();
        }

        return data;
    }

    template std::string          QueryURL<std::string>(const std::string& url);
    template std::vector<uint8_t> QueryURL<std::vector<uint8_t>>(const std::string& url);

    std::vector<uint32_t> CompileGLSLToSPIRV(const char** pSources, int sourceCount, EShLanguage stage, const char* preamble)
    {
        glslang::TShader shader(stage);

        shader.setStrings(pSources, sourceCount);

        // NOTE: We are using SPIR-V 1.1, Vulkan 1.3 style GLSL in this app.
        //       (SPIR-V 1.1+ is needed by spirv_to_dxil)
        //       (Vulkan is needed for easier D3D12 adaption).
        shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_3);
        shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_6);

        if (preamble != nullptr)
            shader.setPreamble(preamble);

        if (!shader.parse(GetDefaultResources(), 450, true, EShMsgEnhanced))
        {
            spdlog::error("GLSL Compilation Failed:\n\n{}", shader.getInfoLog());
            return {};
        }

        glslang::TProgram program;
        program.addShader(&shader);

        if (!program.link(EShMsgDefault))
        {
            spdlog::error("Program Linking Failed:\n\n{}", program.getInfoLog());
            return {};
        }

        std::vector<uint32_t> spirv;
        glslang::GlslangToSpv(*program.getIntermediate(stage), spirv);

        return spirv;
    }

    bool CrossCompileSPIRVToDXIL(const std::string& entryPoint, const std::vector<uint32_t>& spirv, std::vector<uint8_t>& dxil)
    {
        dxil_spirv_debug_options debug_opts = {};
        {
            debug_opts.dump_nir = false;
        }

        struct dxil_spirv_runtime_conf conf;
        memset(&conf, 0, sizeof(conf));
        conf.first_vertex_and_base_instance_mode = DXIL_SPIRV_SYSVAL_TYPE_ZERO;
        conf.declared_read_only_images_as_srvs   = true;
        conf.shader_model_max                    = SHADER_MODEL_6_0;

        dxil_spirv_logger logger = {};
        logger.log               = [](void*, const char* msg) { spdlog::info("{}", msg); };

        dxil_spirv_object dxil_result;

        if (!spirv_to_dxil(spirv.data(),
                           spirv.size(),
                           nullptr,
                           0,
                           DXIL_SPIRV_SHADER_FRAGMENT,
                           entryPoint.c_str(),
                           DXIL_VALIDATOR_1_6,
                           &debug_opts,
                           &conf,
                           &logger,
                           &dxil_result))
        {
            return false;
        }

        // Copy the result to the output byte buffer.
        dxil.resize(dxil_result.binary.size);
        memcpy(dxil.data(), dxil_result.binary.buffer, dxil_result.binary.size);

        spirv_to_dxil_free(&dxil_result);

        return true;
    }

    void ExecuteCommandListAndWait(ID3D12Device*                                   pDevice,
                                   ID3D12CommandQueue*                             pCommandQueue,
                                   std::function<void(ID3D12GraphicsCommandList*)> recordCommandsFunc)
    {
        // Create a command allocator
        ComPtr<ID3D12CommandAllocator> commandAllocator;
        ThrowIfFailed(pDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator)));

        // Create a command list
        ComPtr<ID3D12GraphicsCommandList> commandList;
        ThrowIfFailed(pDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator.Get(), nullptr, IID_PPV_ARGS(&commandList)));

        // Record commands
        recordCommandsFunc(commandList.Get());

        // Close the command list
        ThrowIfFailed(commandList->Close());

        // Execute the command list
        ID3D12CommandList* ppCommandLists[] = { commandList.Get() };
        pCommandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

        // Create a fence for synchronization
        ComPtr<ID3D12Fence> fence;
        ThrowIfFailed(pDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
        HANDLE fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (fenceEvent == nullptr)
        {
            ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
        }

        // Signal and wait for the fence
        const UINT64 fenceValue = 1;
        ThrowIfFailed(pCommandQueue->Signal(fence.Get(), fenceValue));
        if (fence->GetCompletedValue() < fenceValue)
        {
            ThrowIfFailed(fence->SetEventOnCompletion(fenceValue, fenceEvent));
            WaitForSingleObject(fenceEvent, INFINITE);
        }

        CloseHandle(fenceEvent);
    }

    void LoadShaderByteCodes(std::unordered_map<std::string, ComPtr<ID3DBlob>>& shaderByteCodes)
    {
        for (auto& entry : std::filesystem::directory_iterator("Shaders\\"))
        {
            std::vector<uint8_t> shaderDXIL;
            if (!ReadFileBytes(entry.path().string(), shaderDXIL))
                throw std::runtime_error("Failed to load shader bytecode.");

            auto& shaderByteCode = shaderByteCodes[entry.path().stem().string()];
            ThrowIfFailed(D3DCreateBlob(shaderDXIL.size(), &shaderByteCode));
            memcpy(shaderByteCode->GetBufferPointer(), shaderDXIL.data(), shaderDXIL.size());
        }
    }

} // namespace ICR
