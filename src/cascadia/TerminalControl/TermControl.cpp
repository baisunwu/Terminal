#include "pch.h"
#include "TermControl.h"
#include "../../cascadia/terminalcore/Settings.h"
#include <argb.h>

using namespace ::Microsoft::Console::Types;
using namespace winrt::Windows::UI::Xaml;
using namespace winrt::Windows::UI::Core;
using namespace winrt::Windows::System;

namespace winrt::TerminalControl::implementation
{
    Settings s_MakeCoreSettings(const TerminalControl::TerminalSettings& settings)
    {
        Settings result{};
        result.DefaultForeground(settings.DefaultForeground());
        result.DefaultBackground(settings.DefaultBackground());
        // TODO Color Table
        result.HistorySize(settings.HistorySize());
        result.InitialRows(settings.InitialRows());
        result.InitialCols(settings.InitialCols());
        return result;
    }

    TermControl::TermControl() :
        _connection{ TerminalConnection::ConhostConnection(winrt::to_hstring("cmd.exe"), 30, 80) },
        _initializedTerminal{ false },
        _root{ nullptr },
        _controlRoot{ nullptr },
        _swapChainPanel{ nullptr },
        _settings{},
        _closing{ false }
    {
        _Create();
    }

    TermControl::TermControl(TerminalControl::TerminalSettings settings) :
        _connection{ TerminalConnection::ConhostConnection(winrt::to_hstring("cmd.exe"), 30, 80) },
        _initializedTerminal{ false },
        _root{ nullptr },
        _controlRoot{ nullptr },
        _swapChainPanel{ nullptr },
        _settings{ settings },
        _closing{ false }
    {
        _Create();
    }

    void TermControl::_Create()
    {
        // Create a dummy UserControl to use as the "root" of our control we'll
        //      build manually.
        Controls::UserControl myControl;
        _controlRoot = myControl;

        Controls::Grid container;
        _scrollViewer = Controls::ScrollViewer();
        _fakeScrollRoot = Controls::Grid();
        //_fakeScrollRoot.Height(1000);
        //_fakeScrollRoot.Width(150);
        //fakeGrid.Background(Windows::UI::Xaml::Media::SolidColorBrush( Windows::UI::Colors::Yellow()) );
        //Controls::Grid anotherFakeGrid;
        //anotherFakeGrid.Height(100);
        //anotherFakeGrid.Width(300);
        //anotherFakeGrid.Margin(winrt::Windows::UI::Xaml::Thickness{ 0, 50, 0, 0 });
        //anotherFakeGrid.Background(Windows::UI::Xaml::Media::SolidColorBrush(Windows::UI::Colors::Red()));
        // Create the SwapChainPanel that will display our content
        Controls::SwapChainPanel swapChainPanel;
        swapChainPanel.SetValue(FrameworkElement::HorizontalAlignmentProperty(),
            box_value(HorizontalAlignment::Stretch));
        swapChainPanel.SetValue(FrameworkElement::HorizontalAlignmentProperty(),
            box_value(HorizontalAlignment::Stretch));

        swapChainPanel.SizeChanged(std::bind(&TermControl::_SwapChainSizeChanged,
                                             this,
                                             std::placeholders::_1,
                                             std::placeholders::_2));

        // Initialize the terminal only once the swapchainpanel is loaded - that
        //      way, we'll be able to query the real pixel size it got on layout
        swapChainPanel.Loaded([&] (auto /*s*/, auto /*e*/){
            _InitializeTerminal();
        });

        _scrollViewer.ViewChanging([&](auto, const Controls::ScrollViewerViewChangingEventArgs& e) {
            if (_ignoreScrollEvent)
            {
                _ignoreScrollEvent = false;
                return;
            }

            auto next = e.NextView();
            auto offset = next.VerticalOffset();
            auto fin = e.FinalView();
            auto finalOffset = next.VerticalOffset();
            if (offset != finalOffset)
            {
                auto breakHere = 1;
                breakHere;
            }
            auto fakeHeight = _fakeScrollRoot.Height();
            auto fakeBottom = offset + _scrollViewer.ViewportHeight();

            auto viewerHeight = _scrollViewer.ExtentHeight();

            auto bufferHeight = _terminal->GetBufferHeight();
            auto viewRows = _terminal->GetViewport().Height();

            const double fontHeight = _scrollViewer.ViewportHeight() / (double)(viewRows);

            const auto offsetInRows = offset / fontHeight;
            const auto roundedInRows = std::round(offsetInRows);
            const auto roundedRowsToPixels = ((int)(roundedInRows)) * (fontHeight);

            this->ScrollViewport((int)roundedInRows);

        });

        container.Children().Append(swapChainPanel);
        container.Children().Append(_scrollViewer);
        //fakeGrid.Children().Append(swapChainPanel);
        _scrollViewer.Content(_fakeScrollRoot);
        //_fakeScrollRoot.Children().Append(anotherFakeGrid);

        _root = container;
        _swapChainPanel = swapChainPanel;
        _controlRoot.Content(_root);

        _ApplySettings();

        //// These are important:
        // 1. When we get tapped, focus us
        _controlRoot.Tapped([&](auto&, auto& e) {
            _controlRoot.Focus(FocusState::Pointer);
            e.Handled(true);
        });
        // 2. Focus us. (this might not be important
        _controlRoot.Focus(FocusState::Programmatic);
        // 3. Make sure we can be focused (why this isn't `Focusable` I'll never know)
        _controlRoot.IsTabStop(true);
        // 4. Actually not sure about this one. Maybe it isn't necessary either.
        _controlRoot.AllowFocusOnInteraction(true);

        // DON'T CALL _InitializeTerminal here - wait until the swap chain is loaded to do that.
    }

    void TermControl::_ApplySettings()
    {
        winrt::Windows::UI::Color bgColor{};
        uint32_t bg = _settings.DefaultBackground();
        const auto R = GetRValue(bg);
        const auto G = GetGValue(bg);
        const auto B = GetBValue(bg);
        bgColor.R = R;
        bgColor.G = G;
        bgColor.B = B;
        bgColor.A = 255;

        if (_settings.UseAcrylic())
        {
            Media::AcrylicBrush acrylic{};

            acrylic.BackgroundSource(Media::AcrylicBackgroundSource::HostBackdrop);

            acrylic.FallbackColor(bgColor);
            acrylic.TintColor(bgColor);

            acrylic.TintOpacity(_settings.TintOpacity());

            _root.Background(acrylic);

            _settings.DefaultBackground(ARGB(0, R, G, B));
        }
        else
        {
            Media::SolidColorBrush solidColor{};

            solidColor.Color(bgColor);

            _root.Background(solidColor);

            _settings.DefaultBackground(RGB(R, G, B));
        }

    }

    TermControl::~TermControl()
    {
        _closing = true;
        // Don't let anyone else do something to the buffer.
        _terminal->LockForWriting();

        if (_connection != nullptr)
        {
            _connection.Close();
        }

        _renderer->TriggerTeardown();

        _swapChainPanel = nullptr;
        _root = nullptr;
        _connection = nullptr;
    }

    UIElement TermControl::GetRoot()
    {
        return _root;
    }

    Controls::UserControl TermControl::GetControl()
    {
        return _controlRoot;
    }

    void TermControl::SwapChainChanged()
    {
        if (!_initializedTerminal)
        {
            return;
        }

        auto chain = _renderEngine->GetSwapChain();
        _swapChainPanel.Dispatcher().RunAsync(CoreDispatcherPriority::High, [=]()
        {
            _terminal->LockForWriting();
            auto nativePanel = _swapChainPanel.as<ISwapChainPanelNative>();
            nativePanel->SetSwapChain(chain.Get());
            _terminal->UnlockForWriting();
        });
    }

    void TermControl::_InitializeTerminal()
    {
        if (_initializedTerminal)
        {
            return;
        }

        const auto windowWidth = _swapChainPanel.ActualWidth();  // Width() and Height() are NaN?
        const auto windowHeight = _swapChainPanel.ActualHeight();

        _terminal = new ::Microsoft::Terminal::Core::Terminal();

        // First create the render thread.
        auto renderThread = std::make_unique<::Microsoft::Console::Render::RenderThread>();
        // Stash a local pointer to the render thread, so we can enable it after
        //       we hand off ownership to the renderer.
        auto* const localPointerToThread = renderThread.get();
        _renderer = std::make_unique<::Microsoft::Console::Render::Renderer>(_terminal, nullptr, 0, std::move(renderThread));
        ::Microsoft::Console::Render::IRenderTarget& renderTarget = *_renderer;

        // Set up the DX Engine
        auto dxEngine = std::make_unique<::Microsoft::Console::Render::DxEngine>();
        _renderer->AddRenderEngine(dxEngine.get());

        // Prepare the font we want to use from the settings
        const auto* fontFace = _settings.FontFace().c_str();
        const auto* fallbackFontFace = L"Consolas";
        const short fontHeight = gsl::narrow<short>(_settings.FontSize());
        // The font width doesn't terribly matter, we'll only be using the height to look it up
        FontInfoDesired fi(fontFace, 0, 10, { 0, fontHeight }, 437);
        FontInfo actual(fontFace, 0, 10, { 0, fontHeight }, 437, false);
        try
        {
            // TODO: If the font doesn't exist, this doesn't actually fail.
            //      We need a way to gracefully fallback.
            _renderer->TriggerFontChange(96, fi, actual);
        }
        catch (...)
        {
            // The font width doesn't terribly matter, we'll only be using the height to look it up
            FontInfoDesired fiFallback(fallbackFontFace, 0, 10, { 0, fontHeight }, 437);
            FontInfo actualFallback(fallbackFontFace, 0, 10, { 0, fontHeight }, 437, false);
            _renderer->TriggerFontChange(96, fiFallback, actualFallback);
        }

        // Determine the size of the window, in characters.
        // Fist set up the dx engine with the window size in pixels.
        // Then, using the font, get the number of characters that can fit.
        // Resize our terminal connection to match that size, and initialize the terminal with that size.
        const auto viewInPixels = Viewport::FromDimensions({ 0, 0 },
                                                           { static_cast<short>(windowWidth), static_cast<short>(windowHeight) });
        THROW_IF_FAILED(dxEngine->SetWindowSize({ viewInPixels.Width(), viewInPixels.Height() }));
        const auto vp = dxEngine->GetViewportInCharacters(viewInPixels);
        const auto width = vp.Width();
        const auto height = vp.Height();
        _connection.Resize(height, width);

        // Override the default width and height to match the size of the swapChainPanel
        _settings.InitialCols(width);
        _settings.InitialRows(height);

        auto s = s_MakeCoreSettings(_settings);
        _terminal->CreateFromSettings(s, renderTarget);

        // Tell the DX Engine to notify us when the swap chain changes.
        dxEngine->SetCallback(std::bind(&TermControl::SwapChainChanged, this));

        THROW_IF_FAILED(dxEngine->Enable());
        _renderEngine = std::move(dxEngine);

        auto onRecieveOutputFn = [&](const hstring str) {
            _terminal->Write(str.c_str());
        };
        _connectionOutputEventToken = _connection.TerminalOutput(onRecieveOutputFn);

        auto inputFn = std::bind(&TermControl::_SendInputToConnection, this, std::placeholders::_1);
        _terminal->_pfnWriteInput = inputFn;

        THROW_IF_FAILED(localPointerToThread->Initialize(_renderer.get()));

        auto chain = _renderEngine->GetSwapChain();
        _swapChainPanel.Dispatcher().RunAsync(CoreDispatcherPriority::High, [=]()
        {
            _terminal->LockConsole();
            auto nativePanel = _swapChainPanel.as<ISwapChainPanelNative>();
            nativePanel->SetSwapChain(chain.Get());
            _terminal->UnlockConsole();
        });

        // Set up the height of the ScrollViewer and the grid we're using to fake our scrolling height
        auto bottom = _terminal->GetViewport().BottomExclusive();
        auto bufferHeight = bottom;
        COORD fontSize{};
        THROW_IF_FAILED(_renderEngine->GetFontSize(&fontSize));
         
        const auto heightInPixels = bufferHeight * fontSize.Y;
        //const auto heightInPixels = bufferHeight * 16;
        
         _bottomPadding = (int)(windowHeight - heightInPixels);
         const auto realHeightInPixels = heightInPixels;// +_bottomPadding;
        _fakeScrollRoot.Height(realHeightInPixels);
        //_scrollViewer.Height(heightInPixels);
        localPointerToThread->EnablePainting();


        // No matter what order these guys are in, The KeyDown's will fire
        //      before the CharacterRecieved, so we can't easily get characters
        //      first, then fallback to getting keys from vkeys.
        // TODO: This apparently handles keys and characters correctly, though
        //      I'd keep an eye on it, and test more.
        // I presume that the characters that aren't translated by terminalInput
        //      just end up getting ignored, and the rest of the input comes
        //      through CharacterRecieved.
        // I don't believe there's a difference between KeyDown and
        //      PreviewKeyDown for our purposes
        _controlRoot.PreviewKeyDown([&](auto& sender,
            Input::KeyRoutedEventArgs const& e) {
            this->KeyHandler(sender, e);
        });

        _controlRoot.CharacterReceived([&](auto& sender,
            Input::CharacterReceivedRoutedEventArgs const& e) {
            this->CharacterHandler(sender, e);
        });

        _terminal->_pfnTitleChanged = [&](const std::wstring_view& wstr)
        {
            _titleChangeHandlers(winrt::hstring{ wstr });
        };
        _terminal->_pfnScrollPositionChanged = [&](const int viewTop, const int viewHeight, const int bufferSize)
        {
            //auto bottom = _terminal->GetViewport().BottomExclusive();
            auto bottom = viewTop + viewHeight;
            auto bufferHeight2 = bottom;
            auto bufferHeight = _terminal->GetBufferHeight();

            COORD fontSize{};
            THROW_IF_FAILED(_renderEngine->GetFontSize(&fontSize));
            //const auto heightInPixels = bufferHeight * fontSize.Y;
            //const auto heightInPixels = bufferHeight * 16;
            // _fakeScrollRoot.Height(heightInPixels);


            const double fontHeight = _scrollViewer.ViewportHeight() / (double)(viewHeight);
            const auto heightInPixels = bufferHeight * fontHeight;

            const auto offsetInPixels = ((double)(viewTop)) * fontHeight;

            _scrollViewer.Dispatcher().RunAsync(winrt::Windows::UI::Core::CoreDispatcherPriority::Normal, [=](){
                const auto fakeHeight = _fakeScrollRoot.Height();
                
                const auto realHeightInPixels = heightInPixels;// +_bottomPadding;
                _fakeScrollRoot.Height(realHeightInPixels);
                //const auto offsetInPixels = viewTop * 16;
                //_scrollViewer.ChangeView(nullptr, offsetInPixels, nullptr);
                //_scrollViewer.Dispatcher().RunAsync(winrt::Windows::UI::Core::CoreDispatcherPriority::Normal, [=]() {
                //    const auto offsetInPixels = ((double)(viewTop)) * fontHeight;
                    //_scrollViewer.ChangeView(nullptr, offsetInPixels, nullptr);
                //});
                //_scrollViewer.ChangeView(nullptr, offsetInPixels, nullptr);
            });
            //    .Completed([=](auto&&... /*args*/) {

            //    const auto offsetInPixels = ((double)(viewTop)) * fontHeight;
            //    //_scrollViewer.ChangeView(nullptr, offsetInPixels, nullptr, false);

            //    _scrollViewer.Dispatcher().RunAsync(winrt::Windows::UI::Core::CoreDispatcherPriority::Low, [=]() {
            //    //    const auto offsetInPixels = ((double)(viewTop)) * fontHeight;
            //        _scrollViewer.ChangeView(nullptr, offsetInPixels, nullptr);
            //    });
            //});
            _scrollViewer.Dispatcher().RunAsync(winrt::Windows::UI::Core::CoreDispatcherPriority::Low, [=]() {
                //    const auto offsetInPixels = ((double)(viewTop)) * fontHeight;
                _scrollViewer.ChangeView(nullptr, offsetInPixels, nullptr);
            });

            _ignoreScrollEvent = true;
            //_scrollViewer.Dispatcher().RunAsync(winrt::Windows::UI::Core::CoreDispatcherPriority::Low, [=]() {
                //    const auto offsetInPixels = ((double)(viewTop)) * fontHeight;
            //    _scrollViewer.ChangeView(nullptr, offsetInPixels, nullptr);
            //});


            _scrollPositionChangedHandlers(viewTop, viewHeight, bufferSize);
        };

        _connection.Start();
        _initializedTerminal = true;
    }

    void TermControl::CharacterHandler(winrt::Windows::Foundation::IInspectable const& /*sender*/,
                                       Input::CharacterReceivedRoutedEventArgs const& e)
    {
        if (_closing) return;

        const auto ch = e.Character();
        if (ch == L'\x08')
        {
            // We want Backspace to be handled by KeyHandler, so the
            //      terminalInput can translate it into a \x7f. So, do nothing
            //      here, so we don't end up sending both a BS and a DEL to the
            //      terminal.
            return;
        }
        auto hstr = to_hstring(ch);
        _connection.WriteInput(hstr);
        e.Handled(true);
    }

    void TermControl::KeyHandler(winrt::Windows::Foundation::IInspectable const& /*sender*/,
                                 Input::KeyRoutedEventArgs const& e)
    {
        if (_closing) return;
        // This is super hacky - it seems as though these keys only seem pressed
        // every other time they're pressed
        CoreWindow foo = CoreWindow::GetForCurrentThread();
        // DONT USE
        //      != CoreVirtualKeyStates::None
        // OR
        //      == CoreVirtualKeyStates::Down
        // Sometimes with the key down, the state is Down | Locked.
        // Sometimes with the key up, the state is Locked.
        // IsFlagSet(Down) is the only correct solution.
        auto ctrlKeyState = foo.GetKeyState(VirtualKey::Control);
        auto shiftKeyState = foo.GetKeyState(VirtualKey::Shift);
        auto altKeyState = foo.GetKeyState(VirtualKey::Menu);

        auto ctrl = (ctrlKeyState & CoreVirtualKeyStates::Down) == CoreVirtualKeyStates::Down;
        auto shift = (shiftKeyState & CoreVirtualKeyStates::Down) == CoreVirtualKeyStates::Down;
        auto alt = (altKeyState & CoreVirtualKeyStates::Down) == CoreVirtualKeyStates::Down;

        auto vkey = static_cast<WORD>(e.OriginalKey());

        bool handled = false;
        auto bindings = _settings.KeyBindings();
        if (bindings)
        {
            KeyChord chord(ctrl, alt, shift, vkey);
            handled = bindings.TryKeyChord(chord);
        }

        if (!handled)
        {
            _terminal->SendKeyEvent(vkey, ctrl, alt, shift);
        }
        else
        {
            e.Handled(true);
        }
    }

    void TermControl::_SendInputToConnection(const std::wstring& wstr)
    {
        _connection.WriteInput(wstr);
    }

    void TermControl::_SwapChainSizeChanged(winrt::Windows::Foundation::IInspectable const& /*sender*/,
                                            SizeChangedEventArgs const& e)
    {

        if (!_initializedTerminal)
        {
            return;
        }

        _terminal->LockForWriting();
        const auto foundationSize = e.NewSize();
        SIZE classicSize;
        classicSize.cx = (LONG)foundationSize.Width;
        classicSize.cy = (LONG)foundationSize.Height;

        THROW_IF_FAILED(_renderEngine->SetWindowSize(classicSize));
        _renderer->TriggerRedrawAll();
        const auto vp = Viewport::FromInclusive(_renderEngine->GetDirtyRectInChars());

        // If this function succeeds with S_FALSE, then the terminal didn't
        //      actually change size. No need to notify the connection of this
        //      no-op.
        const HRESULT hr = _terminal->UserResize({ vp.Width(), vp.Height() });
        if (SUCCEEDED(hr) && hr != S_FALSE)
        {
            _connection.Resize(vp.Height(), vp.Width());
        }

        _terminal->UnlockForWriting();

    }


    winrt::event_token TermControl::TitleChanged(TerminalControl::TitleChangedEventArgs const& handler)
    {
        return _titleChangeHandlers.add(handler);
    }

    void TermControl::TitleChanged(winrt::event_token const& token) noexcept
    {
        _titleChangeHandlers.remove(token);
    }

    winrt::event_token TermControl::ConnectionClosed(TerminalControl::ConnectionClosedEventArgs const& handler)
    {
        return _connectionClosedHandlers.add(handler);
    }

    void TermControl::ConnectionClosed(winrt::event_token const& token) noexcept
    {
        _connectionClosedHandlers.remove(token);
    }

    hstring TermControl::GetTitle()
    {
        if (!_initializedTerminal) return L"";

        hstring hstr(_terminal->GetConsoleTitle());
        return hstr;
    }
    void TermControl::Close()
    {
        if (!_closing)
        {
            this->~TermControl();
        }
    }

    winrt::event_token TermControl::ScrollPositionChanged(TerminalControl::ScrollPositionChangedEventArgs const& handler)
    {
        return _scrollPositionChangedHandlers.add(handler);
    }

    void TermControl::ScrollPositionChanged(winrt::event_token const& token) noexcept
    {
        _scrollPositionChangedHandlers.remove(token);
    }
    void TermControl::ScrollViewport(int viewTop)
    {
        _terminal->UserScrollViewport(viewTop);
    }
    int TermControl::GetScrollOffset()
    {
        return _terminal->GetScrollOffset();
    }

}
