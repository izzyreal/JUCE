namespace juce
{
struct DxgiAdapter : public ReferenceCountedObject
{
    using Ptr = ReferenceCountedObjectPtr<DxgiAdapter>;

    static Ptr create (ComSmartPtr<ID2D1Factory1> d2dFactory, ComSmartPtr<IDXGIAdapter1> dxgiAdapterIn)
    {
        if (dxgiAdapterIn == nullptr || d2dFactory == nullptr)
            return {};

        Ptr result = new DxgiAdapter;
        result->dxgiAdapter = dxgiAdapterIn;

        for (UINT i = 0;; ++i)
        {
            ComSmartPtr<IDXGIOutput> output;
            const auto hr = result->dxgiAdapter->EnumOutputs (i, output.resetAndGetPointerAddress());

            if (hr == DXGI_ERROR_NOT_FOUND || hr == DXGI_ERROR_NOT_CURRENTLY_AVAILABLE)
                break;

            result->dxgiOutputs.push_back (output);
        }

        const auto creationFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;

        if (const auto hr = D3D11CreateDevice (result->dxgiAdapter,
                                               D3D_DRIVER_TYPE_UNKNOWN,
                                               nullptr,
                                               creationFlags,
                                               nullptr,
                                               0,
                                               D3D11_SDK_VERSION,
                                               result->direct3DDevice.resetAndGetPointerAddress(),
                                               nullptr,
                                               nullptr); FAILED (hr))
        {
            return {};
        }

        if (const auto hr = result->direct3DDevice->QueryInterface (result->dxgiDevice.resetAndGetPointerAddress()); FAILED (hr))
            return {};

        if (const auto hr = d2dFactory->CreateDevice (result->dxgiDevice, result->direct2DDevice.resetAndGetPointerAddress()); FAILED (hr))
            return {};

        return result;
    }

    bool uniqueIDMatches (ReferenceCountedObjectPtr<DxgiAdapter> other) const
    {
        if (other == nullptr)
            return false;

        auto luid = getAdapterUniqueID();
        auto otherLuid = other->getAdapterUniqueID();
        return (luid.HighPart == otherLuid.HighPart) && (luid.LowPart == otherLuid.LowPart);
    }

    LUID getAdapterUniqueID() const
    {
        DXGI_ADAPTER_DESC1 desc;

        if (auto hr = dxgiAdapter->GetDesc1 (&desc); SUCCEEDED (hr))
            return desc.AdapterLuid;

        return LUID { 0, 0 };
    }

    ComSmartPtr<ID3D11Device> direct3DDevice;
    ComSmartPtr<IDXGIDevice> dxgiDevice;
    ComSmartPtr<ID2D1Device1> direct2DDevice;
    ComSmartPtr<IDXGIAdapter1> dxgiAdapter;
    std::vector<ComSmartPtr<IDXGIOutput>> dxgiOutputs;

private:
    DxgiAdapter() = default;
};

struct DxgiAdapterListener
{
    virtual ~DxgiAdapterListener() = default;
    virtual void adapterCreated (DxgiAdapter::Ptr adapter) = 0;
    virtual void adapterRemoved (DxgiAdapter::Ptr adapter) = 0;
};

class DxgiAdapters
{
public:
    explicit DxgiAdapters (ComSmartPtr<ID2D1Factory1> d2dFactoryIn)
        : d2dFactory (d2dFactoryIn)
    {
        updateAdapters();
    }

    ~DxgiAdapters()
    {
        releaseAdapters();
    }

    void updateAdapters()
    {
        if (factory != nullptr && factory->IsCurrent() && ! adapterArray.isEmpty())
            return;

        releaseAdapters();

        if (factory == nullptr || ! factory->IsCurrent())
            factory = makeDxgiFactory();

        if (factory == nullptr)
        {
            jassertfalse;
            return;
        }

        for (UINT i = 0;; ++i)
        {
            ComSmartPtr<IDXGIAdapter1> dxgiAdapter;

            if (factory->EnumAdapters1 (i, dxgiAdapter.resetAndGetPointerAddress()) == DXGI_ERROR_NOT_FOUND)
                break;

            if (const auto adapter = DxgiAdapter::create (d2dFactory, dxgiAdapter))
            {
                adapterArray.add (adapter);
                listeners.call ([adapter] (DxgiAdapterListener& l) { l.adapterCreated (adapter); });
            }
        }
    }

    void releaseAdapters()
    {
        for (const auto& adapter : adapterArray)
            listeners.call ([adapter] (DxgiAdapterListener& l) { l.adapterRemoved (adapter); });

        adapterArray.clear();
    }

    const auto& getAdapterArray() const
    {
        return adapterArray;
    }

    DxgiAdapter::Ptr getDefaultAdapter() const
    {
        return adapterArray.getFirst();
    }

    void addListener (DxgiAdapterListener& l)
    {
        listeners.add (&l);
    }

    void removeListener (DxgiAdapterListener& l)
    {
        listeners.remove (&l);
    }

private:
    static ComSmartPtr<IDXGIFactory1> makeDxgiFactory()
    {
        ComSmartPtr<IDXGIFactory1> result;
        if (const auto hr = CreateDXGIFactory1 (__uuidof (IDXGIFactory1), (void**) result.resetAndGetPointerAddress()); SUCCEEDED (hr))
            return result;

        jassertfalse;
        return {};
    }

    ComSmartPtr<ID2D1Factory1> d2dFactory;
    ThreadSafeListenerList<DxgiAdapterListener> listeners;
    ComSmartPtr<IDXGIFactory1> factory = makeDxgiFactory();
    ReferenceCountedArray<DxgiAdapter> adapterArray;
};

class DirectX
{
public:
    DirectX() = default;

    auto getD2DFactory() const { return d2dSharedFactory; }

private:
    ComSmartPtr<ID2D1Factory1> d2dSharedFactory = [&]
    {
        D2D1_FACTORY_OPTIONS options;
        options.debugLevel = D2D1_DEBUG_LEVEL_NONE;
        ComSmartPtr<ID2D1Factory1> result;
        auto hr = D2D1CreateFactory (D2D1_FACTORY_TYPE_MULTI_THREADED,
                                     __uuidof (ID2D1Factory1),
                                     &options,
                                     (void**) result.resetAndGetPointerAddress());
        jassertquiet (SUCCEEDED (hr));
        return result;
    }();

public:
    DxgiAdapters adapters { d2dSharedFactory };

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DirectX)
};

struct D2DUtilities
{
    template <typename Type>
    static D2D1_RECT_F toRECT_F (const Rectangle<Type>& r)
    {
        return { (float) r.getX(), (float) r.getY(), (float) r.getRight(), (float) r.getBottom() };
    }

    template <typename Type>
    static D2D1_RECT_U toRECT_U (const Rectangle<Type>& r)
    {
        return { (UINT32) r.getX(), (UINT32) r.getY(), (UINT32) r.getRight(), (UINT32) r.getBottom() };
    }

    template <typename Type>
    static RECT toRECT (const Rectangle<Type>& r)
    {
        return { r.getX(), r.getY(), r.getRight(), r.getBottom() };
    }

    static Rectangle<int> toRectangle (const RECT& r)
    {
        return Rectangle<int>::leftTopRightBottom (r.left, r.top, r.right, r.bottom);
    }

    static Point<int> toPoint (POINT p) noexcept          { return { p.x, p.y }; }
    static POINT toPOINT (Point<int> p) noexcept          { return { p.x, p.y }; }

    static D2D1_POINT_2U toPOINT_2U (Point<int> p)        { return D2D1::Point2U ((UINT32) p.x, (UINT32) p.y); }

    static D2D1_COLOR_F toCOLOR_F (Colour c)
    {
        return { c.getFloatRed(), c.getFloatGreen(), c.getFloatBlue(), c.getFloatAlpha() };
    }

    static D2D1::Matrix3x2F transformToMatrix (const AffineTransform& transform)
    {
        return { transform.mat00, transform.mat10, transform.mat01, transform.mat11, transform.mat02, transform.mat12 };
    }

    static Rectangle<int> rectFromSize (D2D1_SIZE_U s)
    {
        return { (int) s.width, (int) s.height };
    }
};

struct Direct2DDeviceContext
{
    static ComSmartPtr<ID2D1DeviceContext1> create (ComSmartPtr<ID2D1Device1> device)
    {
        ComSmartPtr<ID2D1DeviceContext1> result;

        if (const auto hr = device->CreateDeviceContext (D2D1_DEVICE_CONTEXT_OPTIONS_ENABLE_MULTITHREADED_OPTIMIZATIONS,
                                                         result.resetAndGetPointerAddress());
            FAILED (hr))
        {
            jassertfalse;
            return {};
        }

        result->SetTextAntialiasMode (D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
        result->SetAntialiasMode (D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        result->SetUnitMode (D2D1_UNIT_MODE_PIXELS);

        return result;
    }

    static ComSmartPtr<ID2D1DeviceContext1> create (DxgiAdapter::Ptr adapter)
    {
        return adapter != nullptr ? create (adapter->direct2DDevice) : nullptr;
    }

    Direct2DDeviceContext() = delete;
};

struct Direct2DBitmap
{
    Direct2DBitmap() = delete;

    static ComSmartPtr<ID2D1Bitmap1> toBitmap (const Image& image,
                                               ComSmartPtr<ID2D1DeviceContext1> deviceContext,
                                               Image::PixelFormat outputFormat)
    {
        JUCE_D2DMETRICS_SCOPED_ELAPSED_TIME (Direct2DMetricsHub::getInstance()->imageContextMetrics, createBitmapTime);

        jassert (outputFormat == Image::ARGB || outputFormat == Image::SingleChannel);

        JUCE_TRACE_LOG_D2D_PAINT_CALL (etw::createDirect2DBitmapFromImage, etw::graphicsKeyword);

        const auto convertedImage = SoftwareImageType{}.convert (image).convertedToFormat (outputFormat);

        if (! convertedImage.isValid())
            return {};

        Image::BitmapData bitmapData { convertedImage, Image::BitmapData::readWrite };

        D2D1_BITMAP_PROPERTIES1 bitmapProperties{};
        bitmapProperties.pixelFormat.format = outputFormat == Image::SingleChannel
                                              ? DXGI_FORMAT_A8_UNORM
                                              : DXGI_FORMAT_B8G8R8A8_UNORM;
        bitmapProperties.pixelFormat.alphaMode = outputFormat == Image::RGB
                                                 ? D2D1_ALPHA_MODE_IGNORE
                                                 : D2D1_ALPHA_MODE_PREMULTIPLIED;
        bitmapProperties.dpiX = USER_DEFAULT_SCREEN_DPI;
        bitmapProperties.dpiY = USER_DEFAULT_SCREEN_DPI;

        const D2D1_SIZE_U size { (UINT32) image.getWidth(), (UINT32) image.getHeight() };

        ComSmartPtr<ID2D1Bitmap1> bitmap;
        deviceContext->CreateBitmap (size,
                                     bitmapData.data,
                                     (UINT32) bitmapData.lineStride,
                                     bitmapProperties,
                                     bitmap.resetAndGetPointerAddress());
        return bitmap;
    }

    static ComSmartPtr<ID2D1Bitmap1> createBitmap (ComSmartPtr<ID2D1DeviceContext1> deviceContext,
                                                   Image::PixelFormat format,
                                                   D2D_SIZE_U size,
                                                   D2D1_BITMAP_OPTIONS options)
    {
        JUCE_TRACE_LOG_D2D_PAINT_CALL (etw::createDirect2DBitmap, etw::graphicsKeyword);

        JUCE_D2DMETRICS_SCOPED_ELAPSED_TIME (Direct2DMetricsHub::getInstance()->imageContextMetrics, createBitmapTime);

        const auto maxBitmapSize = deviceContext->GetMaximumBitmapSize();
        jassertquiet (size.width <= maxBitmapSize && size.height <= maxBitmapSize);

        const auto pixelFormat = D2D1::PixelFormat (format == Image::SingleChannel
                                                        ? DXGI_FORMAT_A8_UNORM
                                                        : DXGI_FORMAT_B8G8R8A8_UNORM,
                                                    format == Image::RGB
                                                        ? D2D1_ALPHA_MODE_IGNORE
                                                        : D2D1_ALPHA_MODE_PREMULTIPLIED);
        const auto bitmapProperties = D2D1::BitmapProperties1 (options, pixelFormat);

        ComSmartPtr<ID2D1Bitmap1> bitmap;
        const auto hr = deviceContext->CreateBitmap (size,
                                                     {},
                                                     {},
                                                     bitmapProperties,
                                                     bitmap.resetAndGetPointerAddress());

        jassertquiet (SUCCEEDED (hr) && bitmap != nullptr);
        return bitmap;
    }
};

class UpdateRegion
{
public:
    void findRECTAndValidate (HWND windowHandle)
    {
        numRect = 0;

        auto regionHandle = CreateRectRgn (0, 0, 0, 0);

        if (regionHandle == nullptr)
        {
            ValidateRect (windowHandle, nullptr);
            return;
        }

        auto regionType = GetUpdateRgn (windowHandle, regionHandle, false);

        if (regionType == 1)
        {
            const auto bounds = D2DUtilities::toRECT_F (bounds);
            numRect = AddRectRegion (regionHandle, bounds);

            ValidateRect (windowHandle, &bounds);
        }
    }

private:
    RECT bounds {};
};
} // namespace juce
