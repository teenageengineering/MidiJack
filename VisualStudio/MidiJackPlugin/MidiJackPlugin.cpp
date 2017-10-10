#include "stdafx.h"

namespace
{
    // Basic type aliases
    using InputHandle = HMIDIIN;
    using OutputHandle = HMIDIOUT;
    using DeviceID = uint32_t;

    // Utility functions for Win32/64 compatibility
#ifdef _WIN64
    DeviceID InputHandleToID(InputHandle handle)
    {
        return static_cast<DeviceID>(reinterpret_cast<uint64_t>(handle));
    }
    InputHandle DeviceIDToInputHandle(DeviceID id)
    {
        return reinterpret_cast<InputHandle>(static_cast<uint64_t>(id));
    }
    DeviceID OutputHandleToID(OutputHandle handle)
    {
        return static_cast<DeviceID>(reinterpret_cast<uint64_t>(handle));
    }
    OutputHandle DeviceIDToOutputHandle(DeviceID id)
    {
        return reinterpret_cast<OutputHandle>(static_cast<uint64_t>(id));
    }
#else
    DeviceID InputHandleToID(InputHandle handle)
    {
        return reinterpret_cast<DeviceID>(handle);
    }
    InputHandle DeviceIDToInputHandle(DeviceID id)
    {
        return reinterpret_cast<InputHandle>(id);
    }
    DeviceID OutputHandleToID(OutputHandle handle)
    {
        return reinterpret_cast<DeviceID>(handle);
    }
    OutputHandle DeviceIDToOutputHandle(DeviceID id)
    {
        return reinterpret_cast<OutputHandle>(id);
    }
#endif

    // MIDI message storage class
    class MidiMessage
    {
        DeviceID endpoint_;
        uint8_t status_;
        uint8_t data1_;
        uint8_t data2_;

    public:

        MidiMessage(DeviceID endpoint, uint32_t rawData)
            : endpoint_(endpoint), status_(rawData), data1_(rawData >> 8), data2_(rawData >> 16)
        {
        }

        uint64_t Encode64Bit()
        {
            uint64_t ul = endpoint_;
            ul |= (uint64_t)status_ << 32;
            ul |= (uint64_t)data1_ << 40;
            ul |= (uint64_t)data2_ << 48;
            return ul;
        }

        std::string ToString()
        {
            char temp[256];
            std::snprintf(temp, sizeof(temp), "(%X) %02X %02X %02X", endpoint_, status_, data1_, data2_);
            return temp;
        }
    };

    // Incoming MIDI message queue
    std::queue<MidiMessage> message_queue;

    // Device handler lists
    std::list<InputHandle> active_handles_in;
    std::list<OutputHandle> active_handles_out;
    std::stack<InputHandle> handles_to_close_in;
    std::stack<OutputHandle> handles_to_close_out;

    // Mutex for resources
    std::recursive_mutex resource_lock;

    // MIDI input callback
    static void CALLBACK MidiInProc(InputHandle hMidiIn, UINT wMsg, DWORD_PTR dwInstance, DWORD_PTR dwParam1, DWORD_PTR dwParam2)
    {
        if (wMsg == MIM_DATA)
        {
            DeviceID id = InputHandleToID(hMidiIn);
            uint32_t raw = static_cast<uint32_t>(dwParam1);
            resource_lock.lock();
            message_queue.push(MidiMessage(id, raw));
            resource_lock.unlock();
        }
        else if (wMsg == MIM_CLOSE)
        {
            resource_lock.lock();
            handles_to_close_in.push(hMidiIn);
            resource_lock.unlock();
        }
    }
    
    // MIDI output callback
    static void CALLBACK MidiOutProc(OutputHandle hMidiOut, UINT wMsg, DWORD_PTR dwInstance, DWORD_PTR dwParam1, DWORD_PTR dwParam2)
    {
        if (wMsg == MIM_CLOSE)
        {
            resource_lock.lock();
            handles_to_close_out.push(hMidiOut);
            resource_lock.unlock();
        }
    }

    // Retrieve a name of a given source.
    std::string GetSourceName(InputHandle handle)
    {
        auto casted_id = reinterpret_cast<UINT_PTR>(handle);
        MIDIINCAPS caps;
        if (midiInGetDevCaps(casted_id, &caps, sizeof(caps)) == MMSYSERR_NOERROR) {
            std::wstring name(caps.szPname);
            return std::string(name.begin(), name.end());
        }
        return "unknown";
    }
    
    // Retrieve a name of a given destination.
    std::string GetDestinationName(OutputHandle handle)
    {
        auto casted_id = reinterpret_cast<UINT_PTR>(handle);
        MIDIOUTCAPS caps;
        if (midiOutGetDevCaps(casted_id, &caps, sizeof(caps)) == MMSYSERR_NOERROR) {
            std::wstring name(caps.szPname);
            return std::string(name.begin(), name.end());
        }
        return "unknown";
    }

    // Open a MIDI source with a given index.
    void OpenSource(unsigned int index)
    {
        static const DWORD_PTR callback = reinterpret_cast<DWORD_PTR>(MidiInProc);
        InputHandle handle;
        if (midiInOpen(&handle, index, callback, NULL, CALLBACK_FUNCTION) == MMSYSERR_NOERROR)
        {
            if (midiInStart(handle) == MMSYSERR_NOERROR)
            {
                resource_lock.lock();
                active_handles_in.push_back(handle);
                resource_lock.unlock();
            }
            else
            {
                midiInClose(handle);
            }
        }
    }
    
    // Open a MIDI destination with a given index.
    void OpenDestination(unsigned int index)
    {
        static const DWORD_PTR callback = reinterpret_cast<DWORD_PTR>(MidiOutProc);
        OutputHandle handle;
        if (midiOutOpen(&handle, index, callback, NULL, CALLBACK_FUNCTION) == MMSYSERR_NOERROR)
        {
            resource_lock.lock();
            active_handles_out.push_back(handle);
            resource_lock.unlock();
        }
    }

    // Close a given source handler.
    void CloseSource(InputHandle handle)
    {
        midiInClose(handle);

        resource_lock.lock();
        active_handles_in.remove(handle);
        resource_lock.unlock();
    }
    
    // Close a given destination handler.
    void CloseDestination(OutputHandle handle)
    {
        midiOutClose(handle);
        
        resource_lock.lock();
        active_handles_out.remove(handle);
        resource_lock.unlock();
    }

    // Open the all devices.
    void OpenAllDevices()
    {
        int source_count = midiInGetNumDevs();
        for (int i = 0; i < source_count; i++) OpenSource(i);
        
        int destination_count = midiOutGetNumDevs();
        for (int i = 0; i < destination_count; i++) OpenDestination(i);
    }

    // Refresh device handlers
    void RefreshDevices()
    {
        resource_lock.lock();

        // Close disconnected source handlers.
        while (!handles_to_close_in.empty()) {
            CloseSource(handles_to_close_in.top());
            handles_to_close_in.pop();
        }
        
        // Close disconnected destination handlers.
        while (!handles_to_close_out.empty()) {
            CloseDestination(handles_to_close_out.top());
            handles_to_close_out.pop();
        }

        // Try open all devices to detect newly connected ones.
        OpenAllDevices();

        resource_lock.unlock();
    }

    // Close the all devices.
    void CloseAllDevices()
    {
        resource_lock.lock();
        while (!active_handles_in.empty())
            CloseSource(active_handles_in.front());
        
        while (!active_handles_out.empty())
            CloseDestination(active_handles_out.front());
        resource_lock.unlock();
    }
    
    // Send a MIDI message
    void SendMessage(uint64_t msg)
    {
        OutputHandle handle = DeviceIDToOutputHandle((DeviceID)msg);
        DWORD packet = (msg >> 32);
        midiOutShortMsg(handle, packet);
    }
}

// Exported functions

#define EXPORT_API extern "C" __declspec(dllexport)

// Counts the number of sources.
EXPORT_API int MidiJackCountSources()
{
    return static_cast<int>(active_handles_in.size());
}

// Counts the number of destinations.
EXPORT_API int MidiJackCountDestinations()
{
    return static_cast<int>(active_handles_out.size());
}

// Get the unique ID of a source.
EXPORT_API uint32_t MidiJackGetSourceIDAtIndex(int index)
{
    auto itr = active_handles_in.begin();
    std::advance(itr, index);
    return InputHandleToID(*itr);
}

// Get the unique ID of a destination.
EXPORT_API uint32_t MidiJackGetDestinationIDAtIndex(int index)
{
    auto itr = active_handles_out.begin();
    std::advance(itr, index);
    return OutputHandleToID(*itr);
}

// Get the name of a source.
EXPORT_API const char* MidiJackGetSourceName(uint32_t id)
{
    auto handle = DeviceIDToInputHandle(id);
    static std::string buffer;
    buffer = GetSourceName(handle);
    return buffer.c_str();
}

// Get the name of a destination.
EXPORT_API const char* MidiJackGetDestinationName(uint32_t id)
{
    auto handle = DeviceIDToOutputHandle(id);
    static std::string buffer;
    buffer = GetDestinationName(handle);
    return buffer.c_str();
}

// Retrieve and erase an MIDI message data from the message queue.
EXPORT_API uint64_t MidiJackDequeueIncomingData()
{
    RefreshDevices();

    if (message_queue.empty()) return 0;

    resource_lock.lock();
    auto msg = message_queue.front();
    message_queue.pop();
    resource_lock.unlock();

    return msg.Encode64Bit();
}

// Send a MIDI message
EXPORT_API void MidiJackSendMessage(uint64_t msg)
{
    SendMessage(msg);
}
