package com.teenageengineering.midijackplugin;

import android.app.Fragment;
import android.os.Bundle;
import android.content.Context;
import android.media.midi.MidiDevice;
import android.media.midi.MidiDeviceInfo;
import android.media.midi.MidiInputPort;
import android.media.midi.MidiManager;
import android.media.midi.MidiOutputPort;
import android.media.midi.MidiReceiver;

import java.util.List;
import java.util.ArrayList;
import java.util.Queue;
import java.util.concurrent.ArrayBlockingQueue;
import java.util.NoSuchElementException;
import java.nio.ByteBuffer;

import com.unity3d.player.UnityPlayer;

public class PluginEntry extends Fragment {

    //region data classes

    class MidiMessage {
        int mEndpoint;
        byte mStatus;
        byte mData[] = new byte[2];

        MidiMessage(int endpoint, byte status) {
            mEndpoint = endpoint;
            mStatus = status;
        }

        void SetData(byte data[], int offset, int count) {
            if (count > 2) return;
            for (int i = 0; i < count; i++)
                mData[i] = data[offset + i];
        }

        long Encode64Bit() {
            long l = mEndpoint;
            l |= (long)(mStatus & 0xFF) << 32;
            l |= (long)(mData[0] & 0xFF) << 40;
            l |= (long)(mData[1] & 0xFF) << 48;
            return l;
        }
    };

    class MidiJackOutput {
        int mId;
        String mName;

        MidiJackOutput(int id, String name) {
            mId = id;
            mName = name;
        }
    }

    class MidiJackInput {
        int mId;
        String mName;
        MidiInputPort mPort;

        MidiJackInput(int id, String name, MidiInputPort port) {
            mId = id;
            mName = name;
            mPort = port;
        }
    }

    //endregion

    // MIDI endpoint arrays
    public List<MidiJackOutput> mOutputs;
    public List<MidiJackInput> mInputs;

    // Incoming MIDI message queue
    public Queue<MidiMessage> mMessageQueue;

    public MidiManager mMidiManager;

    public PluginEntry() {
        mOutputs = new ArrayList<MidiJackOutput>();
        mInputs = new ArrayList<MidiJackInput>();
        mMessageQueue = new ArrayBlockingQueue<MidiMessage>(1024);
    }

    @Override
    public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setRetainInstance(true); // Retain between configuration changes (like device rotation)
    }

    public void SetMidiManager(MidiManager midiManager) {
        mMidiManager = midiManager;
        mMidiManager.registerDeviceCallback(new MidiJackDeviceCallback(), null);
        // Enumerate all MIDI inputs and outputs
        MidiDeviceInfo[] deviceInfos = instance.mMidiManager.getDevices();
        for (MidiDeviceInfo deviceInfo : deviceInfos)
            AddDevice(deviceInfo);
    }

    public void AddDevice(final MidiDeviceInfo deviceInfo) {
        mMidiManager.openDevice(deviceInfo, new MidiManager.OnDeviceOpenedListener() {
            @Override
            public void onDeviceOpened(MidiDevice device) {
                if (device != null) {
                    MidiDeviceInfo.PortInfo[] portInfos = deviceInfo.getPorts();
                    for (MidiDeviceInfo.PortInfo portInfo : portInfos) {
                        int portId = portInfo.hashCode();
                        String portName = portInfo.getName();
                        if (portInfo.getType() == MidiDeviceInfo.PortInfo.TYPE_OUTPUT) {
                            MidiOutputPort outputPort = device.openOutputPort(portInfo.getPortNumber());
                            outputPort.connect(new MidiFramer(new MidiJackReceiver(portId)));
                            MidiJackOutput output = new MidiJackOutput(portId, portName);
                            mOutputs.add(output);
                        } else {
                            MidiInputPort inputPort = device.openInputPort(portInfo.getPortNumber());
                            MidiJackInput input = new MidiJackInput(portId, portName, inputPort);
                            mInputs.add(input);
                        }
                    }
                }
            }
        }, null);
    }

    public void RemoveDevice(final MidiDeviceInfo deviceInfo) {
        MidiDeviceInfo.PortInfo[] portInfos = deviceInfo.getPorts();
        for (MidiDeviceInfo.PortInfo portInfo : portInfos) {
            int portId = portInfo.hashCode();
            String portName = portInfo.getName();
            if (portInfo.getType() == MidiDeviceInfo.PortInfo.TYPE_OUTPUT) {
                MidiJackOutput output = GetOutputWithId(portId);
                mOutputs.remove(output);
            } else {
                MidiJackInput input = GetInputWithId(portId);
                mInputs.remove(input);
            }
        }
    }

    public MidiJackOutput GetOutputWithId(int id) throws NoSuchElementException {
        for (MidiJackOutput output : mOutputs)
            if (output.mId == id)
                return output;
        throw new NoSuchElementException();
    }

    public MidiJackInput GetInputWithId(int id) throws NoSuchElementException {
        for (MidiJackInput input : mInputs)
            if (input.mId == id)
                return input;
        throw new NoSuchElementException();
    }

    //region callback classes

    static class MidiJackDeviceCallback extends MidiManager.DeviceCallback {
        public void onDeviceAdded(MidiDeviceInfo deviceInfo) {
            instance.AddDevice(deviceInfo);
        }
        public void onDeviceRemoved(MidiDeviceInfo deviceInfo) { instance.RemoveDevice(deviceInfo); }
    }

    class MidiJackReceiver extends MidiReceiver {
        int mInputId;

        public MidiJackReceiver(int inputId) {
            mInputId = inputId;
        }

        @Override
        public void onSend(byte[] msg, int start, int count, long timestamp) {
            int status = msg[start] & 0xFF;
            if (status == MidiConstants.STATUS_SYSTEM_EXCLUSIVE && (int)(msg[start + count - 1] & 0xFF) == MidiConstants.STATUS_END_SYSEX) {
                if ((int)(msg[start + 1] & 0xFF) == 0x00 &&
                    (int)(msg[start + 2] & 0xFF) == 0x20 &&  // teenage
                    (int)(msg[start + 3] & 0xFF) == 0x76 &&  // engineering
                    (int)(msg[start + 4] & 0xFF) == 0x3)     // videolab
                {
                    MidiMessage message = new MidiMessage(mInputId, msg[start]);
                    message.SetData(msg, start + 5, 2);
                    instance.mMessageQueue.add(message);
                }
            } else if (status >= MidiConstants.STATUS_NOTE_OFF) {
                MidiMessage message = new MidiMessage(mInputId, msg[start]);
                message.SetData(msg, start + 1, count - 1);
                instance.mMessageQueue.add(message);
            }
        }
    }

    //endregion

    //region plugin interface

    private static PluginEntry instance;

    public static void MidiJackStart() {
        instance = new PluginEntry();
        instance.SetMidiManager((MidiManager)UnityPlayer.currentActivity.getSystemService(Context.MIDI_SERVICE));
        UnityPlayer.currentActivity.getFragmentManager().beginTransaction().add(instance, "PluginEntry").commit();
    }

    public static int MidiJackCountSources() {
        try {
            return instance.mOutputs.size();
        } catch (Exception e) {
            return 0;
        }
    }

    public static int MidiJackCountDestinations() {
        try {
            return instance.mInputs.size();
        } catch (Exception e) {
            return 0;
        }
    }

    public static int MidiJackGetSourceIdAtIndex(int index) {
        try {
            return instance.mOutputs.get(index).mId;
        } catch (Exception e) {
            return 0;
        }
    }

    public static int MidiJackGetDestinationIdAtIndex(int index) {
        try {
            return instance.mInputs.get(index).mId;
        } catch (Exception e) {
            return 0;
        }
    }

    public static String MidiJackGetSourceName(int id) {
        try {
            return instance.GetOutputWithId(id).mName;
        } catch (Exception e) {
            return "(not ready)";
        }
    }

    public static String MidiJackGetDestinationName(int id) {
        try {
            return instance.GetInputWithId(id).mName;
        } catch (Exception e) {
            return "(not ready)";
        }
    }

    public static long MidiJackDequeueIncomingData() {
        try {
            return instance.mMessageQueue.remove().Encode64Bit();
        } catch (Exception e) {
            return 0;
        }
    }

    public static void MidiJackSendMessage(long msg) {
        int inputId = (int)msg;
        try {
            ByteBuffer buffer = ByteBuffer.allocate(Long.BYTES);
            buffer.putLong(msg);
            MidiJackInput input = instance.GetInputWithId(inputId);
            input.mPort.send(buffer.array(), 4, 3);
        } catch (Exception e) {}
    }

    //endregion
}