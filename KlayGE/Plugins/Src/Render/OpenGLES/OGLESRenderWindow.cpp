// OGLESRenderWindow.cpp
// KlayGE OpenGL ES 2��Ⱦ������ ʵ���ļ�
// Ver 3.10.0
// ��Ȩ����(C) ������, 2010
// Homepage: http://www.klayge.org
//
// 3.10.0
// ���ν��� (2010.1.22)
//
// �޸ļ�¼
//////////////////////////////////////////////////////////////////////////////////

#include <KlayGE/KlayGE.hpp>
#include <KlayGE/ThrowErr.hpp>
#include <KlayGE/Util.hpp>
#include <KlayGE/Math.hpp>
#include <KlayGE/SceneManager.hpp>
#include <KlayGE/Context.hpp>
#include <KlayGE/RenderSettings.hpp>
#include <KlayGE/RenderFactory.hpp>
#include <KlayGE/RenderEngine.hpp>
#include <KlayGE/App3D.hpp>
#include <KlayGE/Window.hpp>

#include <map>
#include <sstream>
#include <boost/assert.hpp>
#include <boost/bind.hpp>

#include <glloader/glloader.h>

#include <KlayGE/OpenGLES/OGLESRenderWindow.hpp>

namespace KlayGE
{
	OGLESRenderWindow::OGLESRenderWindow(std::string const & name, RenderSettings const & settings)
						: OGLESFrameBuffer(false),
							ready_(false), closed_(false)
	{
		// Store info
		name_				= name;
		width_				= settings.width;
		height_				= settings.height;
		isFullScreen_		= settings.full_screen;
		color_bits_			= NumFormatBits(settings.color_fmt);

		WindowPtr main_wnd = Context::Instance().AppInstance().MainWnd();
		main_wnd->OnActive().connect(boost::bind(&OGLESRenderWindow::OnActive, this, _1, _2));
		main_wnd->OnPaint().connect(boost::bind(&OGLESRenderWindow::OnPaint, this, _1));
		main_wnd->OnEnterSizeMove().connect(boost::bind(&OGLESRenderWindow::OnEnterSizeMove, this, _1));
		main_wnd->OnExitSizeMove().connect(boost::bind(&OGLESRenderWindow::OnExitSizeMove, this, _1));
		main_wnd->OnSize().connect(boost::bind(&OGLESRenderWindow::OnSize, this, _1, _2));
		main_wnd->OnClose().connect(boost::bind(&OGLESRenderWindow::OnClose, this, _1));

		if (isFullScreen_)
		{
			left_ = 0;
			top_ = 0;
		}
		else
		{
			top_ = settings.top;
			left_ = settings.left;
		}

		display_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);

		int r_size, g_size, b_size, a_size, d_size, s_size;
		switch (settings.color_fmt)
		{
		case EF_ARGB8:
		case EF_ABGR8:
			r_size = 8;
			g_size = 8;
			b_size = 8;
			a_size = 8;
			break;

		case EF_A2BGR10:
			r_size = 10;
			g_size = 10;
			b_size = 10;
			a_size = 2;
			break;

		default:
			BOOST_ASSERT(false);
			break;
		}
		switch (settings.depth_stencil_fmt)
		{
		case EF_D16:
			d_size = 16;
			s_size = 0;
			break;

		case EF_D24S8:
			d_size = 24;
			s_size = 8;
			break;

		case EF_D32F:
			d_size = 32;
			s_size = 0;
			break;

		default:
			d_size = 0;
			s_size = 0;
			break;
		}

		std::vector<EGLint> visual_attr;
		visual_attr.push_back(EGL_RENDERABLE_TYPE);
		visual_attr.push_back(EGL_OPENGL_ES2_BIT);
		visual_attr.push_back(EGL_RED_SIZE);
		visual_attr.push_back(r_size);
		visual_attr.push_back(EGL_GREEN_SIZE);
		visual_attr.push_back(g_size);
		visual_attr.push_back(EGL_BLUE_SIZE);
		visual_attr.push_back(b_size);
		visual_attr.push_back(EGL_ALPHA_SIZE);
		visual_attr.push_back(a_size);
		if (d_size > 0)
		{
			visual_attr.push_back(EGL_DEPTH_SIZE);
			visual_attr.push_back(d_size);
		}
		if (s_size > 0)
		{
			visual_attr.push_back(EGL_STENCIL_SIZE);
			visual_attr.push_back(s_size);
		}
		if (settings.sample_count > 1)
		{
			visual_attr.push_back(EGL_SAMPLES);
			visual_attr.push_back(settings.sample_count);
		}
		visual_attr.push_back(EGL_NONE);				// end of list

		EGLint major_ver, minor_ver;
		EGLint num_cfgs;
		eglInitialize(display_, &major_ver, &minor_ver);
		eglGetConfigs(display_, NULL, 0, &num_cfgs);

		eglChooseConfig(display_, &visual_attr[0], &cfg_, 1, &num_cfgs);

		NativeWindowType wnd;
#if defined KLAYGE_PLATFORM_WINDOWS
		wnd = hWnd_ = main_wnd->HWnd();
		hDC_ = ::GetDC(hWnd_);
#elif defined KLAYGE_PLATFORM_LINUX
		wnd = x_window_ = main_wnd->XWindow();
#elif defined KLAYGE_PLATFORM_ANDROID
		wnd = a_window_ = main_wnd->AWindow();
		EGLint format;
		eglGetConfigAttrib(display_, cfg_, EGL_NATIVE_VISUAL_ID, &format);
		ANativeWindow_setBuffersGeometry(wnd, 0, 0, format);
#endif

		surf_ = eglCreateWindowSurface(display_, cfg_, wnd, NULL);

		EGLint ctx_attr[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
		context_ = eglCreateContext(display_, cfg_, EGL_NO_CONTEXT, ctx_attr);
   
		eglMakeCurrent(display_, surf_, surf_, context_);

		if (!glloader_GLES_VERSION_2_0())
		{
			THR(boost::system::posix_error::not_supported);
		}

		eglSwapInterval(display_, 0);

		glPixelStorei(GL_PACK_ALIGNMENT, 1);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

		viewport_.left = 0;
		viewport_.top = 0;
		viewport_.width = width_;
		viewport_.height = height_;

		std::wstring vendor, renderer, version;
		Convert(vendor, reinterpret_cast<char const *>(glGetString(GL_VENDOR)));
		Convert(renderer, reinterpret_cast<char const *>(glGetString(GL_RENDERER)));
		Convert(version, reinterpret_cast<char const *>(glGetString(GL_VERSION)));
		std::wostringstream oss;
		oss << vendor << L" " << renderer << L" " << version;
		if (settings.sample_count > 1)
		{
			oss << L" (" << settings.sample_count << L"x AA)";
		}
		description_ = oss.str();

		active_ = true;
		ready_ = true;
	}

	OGLESRenderWindow::~OGLESRenderWindow()
	{
		WindowPtr main_wnd = Context::Instance().AppInstance().MainWnd();
		main_wnd->OnActive().disconnect(boost::bind(&OGLESRenderWindow::OnActive, this, _1, _2));
		main_wnd->OnPaint().disconnect(boost::bind(&OGLESRenderWindow::OnPaint, this, _1));
		main_wnd->OnEnterSizeMove().disconnect(boost::bind(&OGLESRenderWindow::OnEnterSizeMove, this, _1));
		main_wnd->OnExitSizeMove().disconnect(boost::bind(&OGLESRenderWindow::OnExitSizeMove, this, _1));
		main_wnd->OnSize().disconnect(boost::bind(&OGLESRenderWindow::OnSize, this, _1, _2));
		main_wnd->OnClose().disconnect(boost::bind(&OGLESRenderWindow::OnClose, this, _1));

		this->Destroy();
	}

	bool OGLESRenderWindow::Closed() const
	{
		return closed_;
	}

	bool OGLESRenderWindow::Ready() const
	{
		return ready_;
	}

	void OGLESRenderWindow::Ready(bool ready)
	{
		ready_ = ready;
	}

	std::wstring const & OGLESRenderWindow::Description() const
	{
		return description_;
	}

	// �ı䴰�ڴ�С
	/////////////////////////////////////////////////////////////////////////////////
	void OGLESRenderWindow::Resize(uint32_t width, uint32_t height)
	{
		width_ = width;
		height_ = height;

		// Notify viewports of resize
		viewport_.width = width;
		viewport_.height = height;

		App3DFramework& app = Context::Instance().AppInstance();
		app.OnResize(width, height);
	}

	// �ı䴰��λ��
	/////////////////////////////////////////////////////////////////////////////////
	void OGLESRenderWindow::Reposition(uint32_t left, uint32_t top)
	{
		left_ = left;
		top_ = top;
	}

	// ��ȡ�Ƿ���ȫ��״̬
	/////////////////////////////////////////////////////////////////////////////////
	bool OGLESRenderWindow::FullScreen() const
	{
		return isFullScreen_;
	}

	// �����Ƿ���ȫ��״̬
	/////////////////////////////////////////////////////////////////////////////////
	void OGLESRenderWindow::FullScreen(bool fs)
	{
		if (isFullScreen_ != fs)
		{
			left_ = 0;
			top_ = 0;

#if defined KLAYGE_PLATFORM_WINDOWS
			uint32_t style;
			if (fs)
			{
				DEVMODE devMode;
				devMode.dmSize = sizeof(devMode);
				devMode.dmBitsPerPel = color_bits_;
				devMode.dmPelsWidth = width_;
				devMode.dmPelsHeight = height_;
				devMode.dmFields = DM_BITSPERPEL | DM_PELSWIDTH | DM_PELSHEIGHT;
				::ChangeDisplaySettings(&devMode, CDS_FULLSCREEN);

				style = WS_POPUP;
			}
			else
			{
				::ChangeDisplaySettings(NULL, 0);

				style = WS_OVERLAPPEDWINDOW;
			}

			::SetWindowLongPtrW(hWnd_, GWL_STYLE, style);

			RECT rc = { 0, 0, width_, height_ };
			::AdjustWindowRect(&rc, style, false);
			width_ = rc.right - rc.left;
			height_ = rc.bottom - rc.top;

			isFullScreen_ = fs;

			::ShowWindow(hWnd_, SW_SHOWNORMAL);
			::UpdateWindow(hWnd_);
#elif defined KLAYGE_PLATFORM_LINUX
			isFullScreen_ = fs;
			XFlush(x_display_);
#elif defined KLAYGE_PLATFORM_ANDROID
			isFullScreen_ = fs;
#endif
		}
	}

	void OGLESRenderWindow::WindowMovedOrResized(Window const & win)
	{
#if defined KLAYGE_PLATFORM_WINDOWS
		UNREF_PARAM(win);

		::RECT rect;
		::GetClientRect(hWnd_, &rect);

		uint32_t new_left = rect.left;
		uint32_t new_top = rect.top;
		if ((new_left != left_) || (new_top != top_))
		{
			this->Reposition(new_left, new_top);
		}

		uint32_t new_width = rect.right - rect.left;
		uint32_t new_height = rect.bottom - rect.top;
#elif defined KLAYGE_PLATFORM_LINUX
		int screen = DefaultScreen(x_display_);
		uint32_t new_width = DisplayWidth(x_display_, screen);
		uint32_t new_height = DisplayHeight(x_display_, screen);
#elif defined KLAYGE_PLATFORM_ANDROID
		uint32_t new_left = win.Left() / 2;
		uint32_t new_top = win.Top() / 2;
		if ((new_left != left_) || (new_top != top_))
		{
			this->Reposition(new_left, new_top);
		}

		EGLint new_width, new_height;
		eglQuerySurface(display_, surf_, EGL_WIDTH, &new_width);
		eglQuerySurface(display_, surf_, EGL_HEIGHT, &new_height);

		new_width -= new_left;
		new_height -= new_top;
#endif

		if ((new_width != width_) || (new_height != height_))
		{
			Context::Instance().RenderFactoryInstance().RenderEngineInstance().Resize(new_width, new_height);
		}
	}

	void OGLESRenderWindow::Destroy()
	{
#if defined KLAYGE_PLATFORM_WINDOWS
		if (hWnd_ != NULL)
		{
			::ReleaseDC(hWnd_, hDC_);

			if (isFullScreen_)
			{
				::ChangeDisplaySettings(NULL, 0);
				ShowCursor(TRUE);
			}
		}
#elif defined KLAYGE_PLATFORM_LINUX
#elif defined KLAYGE_PLATFORM_ANDROID
#endif

		if (display_ != NULL)
		{
			eglDestroyContext(display_, context_);
			eglTerminate(display_);

			display_ = NULL;
		}
	}

	void OGLESRenderWindow::SwapBuffers()
	{
		eglSwapBuffers(display_, surf_);
	}

	void OGLESRenderWindow::OnActive(Window const & /*win*/, bool active)
	{
		active_ = active;
	}

	void OGLESRenderWindow::OnPaint(Window const & /*win*/)
	{
		// If we get WM_PAINT messges, it usually means our window was
		// comvered up, so we need to refresh it by re-showing the contents
		// of the current frame.
		if (this->Active() && this->Ready())
		{
			Context::Instance().SceneManagerInstance().Update();
			this->SwapBuffers();
		}
	}

	void OGLESRenderWindow::OnEnterSizeMove(Window const & /*win*/)
	{
		// Previent rendering while moving / sizing
		this->Ready(false);
	}

	void OGLESRenderWindow::OnExitSizeMove(Window const & win)
	{
		this->WindowMovedOrResized(win);
		this->Ready(true);
	}

	void OGLESRenderWindow::OnSize(Window const & win, bool active)
	{
		if (!active)
		{
			active_ = false;
		}
		else
		{
			active_ = true;
			if (this->Ready())
			{
				this->WindowMovedOrResized(win);
			}
		}
	}

	void OGLESRenderWindow::OnClose(Window const & /*win*/)
	{
		this->Destroy();
		closed_ = true;
	}
}