#pragma once

#include <cstdint>

#if !defined(WINDOWER_INTERFACE_VERSION)
#define WINDOWER_INTERFACE_VERSION 0x04070300
#endif

struct MMFSettingsHandler {
    char ProfileName[256];
    char Branch[32];
    char Pad0[16];
    char LauncherVersion[16];
    char HookVersion[16];
    char PlayOnlinePath[4096];
    char FfxiPath[4096];
    char WindowerPath[4096];
    char ConsoleKey[16];
    std::uint32_t Region;
    int Width;
    int Height;
    int UIWidth;
    int UIHeight;
    int XPosition;
    int YPosition;
    bool Fullscreen;
    bool Borderless;
    bool Debug;
    bool Loaded;
    bool AlwaysEnableGamepad;
    bool AllowWinKey;
    std::uint8_t Pad1[2];
};

class Console {
public:
    virtual void __stdcall OpenConsole(bool);
    virtual bool __stdcall IsVisible();
    virtual void __stdcall SetPosition(float, float);
    virtual void __stdcall Write(const char*);
    virtual void __stdcall Clear();
    virtual void __stdcall SendCommand(const char*, bool);
};

class TextHandler;
class PrimitiveHandler;
class PacketStreamHandler;
class FFXI;

class PluginManager {
public:
    virtual MMFSettingsHandler* __stdcall GetMMFSettingsHandler(MMFSettingsHandler*);
    virtual void* __stdcall GetHWND();
    virtual void* __stdcall GetDirect3D8Device();
    virtual Console* __stdcall GetConsole();
    virtual TextHandler* __stdcall GetTextHandler();
    virtual PrimitiveHandler* __stdcall GetPrimitiveHandler();
    virtual PacketStreamHandler* __stdcall GetPacketStreamHandler();
    virtual FFXI* __stdcall GetFFXI();
    virtual PluginManager* __thiscall Dtor(std::uint8_t);
};

class PluginBase {
public:
    virtual const char* __stdcall GetPluginAuthor() = 0;
    virtual const char* __stdcall GetPluginName() = 0;

    virtual void __stdcall Load(PluginManager* manager) { plugin_manager_ = manager; }
    virtual void __stdcall Unload() {}
    virtual bool __stdcall IgnoreUnload() { return false; }
    virtual void __stdcall PreRender() {}
    virtual void __stdcall PostRender() {}
    virtual void __stdcall PluginCommand(const char*) {}
    virtual bool __stdcall UnhandledCommand(const char*) { return false; }
    virtual void __stdcall IncomingText(void*, void*, void*) {}
    virtual void __stdcall OutgoingText(void*, void*, void*) {}
    virtual bool __stdcall IncomingChunk(void*, void*, void*, bool modified) { return modified; }
    virtual bool __stdcall OutgoingChunk(void*, void*, void*, bool modified) { return modified; }
    virtual bool __stdcall Mouse(void*, void*, void*, void*, bool modified) { return modified; }
    virtual bool __stdcall Keyboard(void*, void*, bool modified) { return modified; }
    virtual void __stdcall AddItem(void*, void*, void*, void*) {}
    virtual void __stdcall RemoveItem(void*, void*, void*, void*) {}
    virtual PluginBase* __thiscall Dtor(std::uint8_t) { return this; }

protected:
    PluginManager* plugin_manager_ = nullptr;
};

extern "C" {
    std::uint32_t GetInterfaceVersion();
    PluginBase* CreateInstance();
}
