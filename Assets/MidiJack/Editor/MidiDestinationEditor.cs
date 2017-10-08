using System;
using System.Collections;
using System.Collections.Generic;
using UnityEngine;
using UnityEditor;

namespace MidiJack
{
	[CustomEditor(typeof(MidiDestination))]
	public class MidiDestinationEditor : Editor {

		public override void OnInspectorGUI()
		{
			MidiDestination destination = target as MidiDestination;

			var destinationCount = MidiDriver.CountDestinations();

			List<uint> destinationIds = new List<uint>();
			List<string> destinationNames = new List<string>();
			for (var i = 0; i < destinationCount; i++)
			{
				var id = MidiDriver.GetDestinationIdAtIndex(i);
				destinationIds.Add(id);
				destinationNames.Add(MidiDriver.GetEndpointName(id));
			}

			int destinationIndex = destinationIds.FindIndex(x => x == destination.endpointId);

			// Show name of missing endpoint.
			if (destinationIndex == -1)
			{
				destinationNames.Add(destination.endpointName + " *");
				destinationIndex = destinationCount;
			}

			EditorGUI.BeginChangeCheck();
			destinationIndex = EditorGUILayout.Popup("Destination", destinationIndex, destinationNames.ToArray());
			if (EditorGUI.EndChangeCheck())
			{
				if (destinationIndex != destinationCount)
					destination.endpointId = MidiDriver.GetDestinationIdAtIndex(destinationIndex);
			}
		}
	}
}
