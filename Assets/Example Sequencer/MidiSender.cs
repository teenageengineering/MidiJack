using System.Collections;
using System.Collections.Generic;
using UnityEngine;
using MidiJack;

public class MidiSender : MonoBehaviour {

	public void OnNoteOnEvent()
	{
		MidiMaster.SendKeyDown(0, 60, 1);
	}

	public void OnNoteOffEvent()
	{
		MidiMaster.SendKeyUp(0, 60);
	}
}