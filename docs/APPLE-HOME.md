# Apple Home and a Matter air purifier

What Apple Home does with this firmware. iOS 27, Apple TV as home hub, Levoit Core 300S.

Apple documents almost none of it. "Observed" means we watched it happen, mostly by logging what Home sent. "Reported" means another developer says so and we did not check.

## Short version

| Question | Answer | Basis |
|---|---|---|
| Does Home show an Air Purifier properly? | Yes | Observed |
| Does Home show an Air Quality Sensor? | Yes | Observed |
| Can a device name its own parts? | No | Observed, reported |
| Can a device set its default name? | No | Observed, reported |
| Can air quality read "Excellent"? | No. Home shows only Good, Fair and Poor for Matter. | Observed |
| Can you reset the filter from Home? | No | Observed |
| Will Home deliver firmware updates? | Not without a vendor account | Apple's guide |
| Does Home notice an update changed something? | Yes, if the version number went up | Observed |

## What renders

- An Air Purifier endpoint becomes a tile with a speed slider, a mode menu of Auto, Manual and Off, Filter Condition and Filter Life.
- An Air Quality Sensor endpoint becomes a tile with the level and PM2.5.
- Each On/Off Plug-in Unit endpoint becomes an outlet tile. In Australian English that is "Power point".
- Filter Life is a bare number with no percent sign.
- Manufacturer and Model show Basic Information's VendorName and ProductName, 32 characters each.
- The accessory page says it "has not been certified to work with HomeKit". That is the test vendor ID and test attestation certificate.

## Names

### Parts of a device

Three On/Off endpoints arrive as "Power point 2", "Power point 3" and "Power point", which are endpoints 3, 4 and 5: Sleep, Display and Child Lock. The unnumbered name goes to the last endpoint, not the first. Seen on one pairing under iOS 27. You rename them by hand and lose the names on a re-pair. [FLASHING.md](../FLASHING.md#rename-the-switches) has the steps.

| Mechanism | Result | Basis |
|---|---|---|
| Fixed Label cluster | Ignored | Reported. A developer set it, checked it with chip-tool, saw nothing change. [esp-matter #917](https://github.com/espressif/esp-matter/issues/917) |
| User Label cluster | Ignored | Reported. [Apple forum 788551](https://developer.apple.com/forums/thread/788551) |
| Descriptor TagList with a label per tag | Ignored | Observed. We tagged them "Sleep", "Display" and "Child Lock" and re-paired. Also [matterbridge-hass #86](https://github.com/Luligu/matterbridge-hass/discussions/86), and an Apple engineer saying tags are not a supported way to shape presentation, [forum 852549](https://developer.apple.com/forums/thread/852549). |
| NodeLabel on bridged devices behind an Aggregator | Shown | Reported by two developers, [esp-matter #914](https://github.com/espressif/esp-matter/issues/914). We did not try it. |

The labels are really there. A second controller, the matter.js shell, read "Sleep", "Display" and "Child Lock" back off the three endpoints. Home has them and does not use them.

The bridge route would probably work, and we did not take it. The switches would become three separate accessories, Home would list the purifier as a bridge, and the one report we found of a bridge sharing a node with an ordinary endpoint is of it breaking, [connectedhomeip #30481](https://github.com/project-chip/connectedhomeip/issues/30481).

### The accessory's default name

Always "Matter Accessory". Setting ProductName and VendorName changes the Manufacturer and Model rows and nothing else. Others found the same, [esp-matter #246](https://github.com/espressif/esp-matter/issues/246).

## What Home sends to the fan

Observed. The firmware logs every write and which attribute it hit.

| You do | Home sends |
|---|---|
| Tap Off | FanMode Off, or the Off command if the endpoint has On/Off |
| Tap Manual while off, no On/Off cluster | FanMode High |
| Tap Auto while off, no On/Off cluster | FanMode Auto and FanMode High, 40 ms apart, in either order |
| Tap Auto while off, with an On/Off cluster | On, then FanMode Auto |
| Tap Manual while off, with an On/Off cluster | On, then FanMode High, 50 ms apart |
| Drag the slider | A stream of PercentSetting writes |
| "Turn the purifier on" | On, if the endpoint has On/Off |

### Give the purifier an On/Off cluster

Home has no Low, Medium or High control. Its mode menu only writes Off, Auto or High, and without an On/Off cluster it uses High to mean "turn on". Tap Auto from off and you get two mode writes that contradict each other, in an order that varies. Obey them in sequence and the purifier lands in manual about half the time.

We first papered over this with timing rules. Then we added an On/Off cluster to the purifier endpoint, Home started using it for power, the High writes stopped, and the timing rules came out.

The cluster has to accept On and Toggle. esp-matter's `cluster::on_off::create` registers only Off, because On is conditional in the spec. Call `command::create_on` and `command::create_toggle` yourself. Miss that and Home's On comes back "unsupported command" and Siri says "no response".

### The slider

Home sends raw percentages, several per drag. Matter says round up to the next speed, so with three speeds 34% is speed 2. Follow that. We tried rounding to nearest and it made things worse.

Home redraws the slider at the stepped value only when your final position changed the fan's actual speed. We checked two screen recordings frame by frame against the log. Every move got the stepped value sent back. Home redrew about a third of them, and those were exactly the moves where a new current-speed report followed the last write. If the speed was already right, or changed mid-drag, the slider stays where your finger left it until the app is reopened.

Sending the stepped value back faster did nothing. Sending it again later did nothing. The purifier and the room tile are right throughout.

Home does the same with a switch. Turn one on that the device immediately reports as off, and Home shows it on for a few seconds before it gives in.

## Air quality

Home has five levels and uses three of them for Matter. Observed: we forced each Matter value in turn, with PM2.5 held at 20, and recorded the screen.

| Matter sends | Home shows |
|---|---|
| Good | Good |
| Fair | Fair |
| Moderate | Fair |
| Poor | Poor |
| Very Poor | Poor |
| Extremely Poor | Poor |
| Unknown | Unknown |

- "Excellent" and "Inferior" cannot be reached from Matter. An automation on either never fires.
- Moderate looks the same as Fair, and the two worst values look the same as Poor. A device with more than three levels loses the difference in Home.
- A purifier with four levels has to double up. We send the best two as Good, so that Fair in Home means the air has actually got worse.
- Home does not work the level out from PM2.5. With the level held at Good we sent a PM2.5 of 1 and then 300. Home showed the number and kept saying Good.

## Wind settings

Home does not show Fan Control's wind settings. Reported, [Apple forum 837127](https://developer.apple.com/forums/thread/837127). We set sleep wind on the purifier for other controllers and saw nothing change in Home. It works from the matter.js shell: writing it puts the purifier in Sleep.

## Filter

Home shows HEPA Filter Monitoring's Condition as "Filter Life" and ChangeIndication as "Filter Condition". It has no button for ResetCondition.

## Siri

Siri has no idea what a purifier's sleep mode is. "Turn on sleep mode on the air purifier" turned the purifier off. An extra On/Off endpoint is reachable by the name you gave its tile, or through a scene.

## Identify

The naming screen during pairing has an Identify button. With IdentifyType left at the SDK default of None it did nothing. We set VisibleIndicator and implemented the command. Not yet confirmed that the button works.

## Pairing

- Bluetooth pairing with Wi-Fi handover works with the SDK's defaults.
- Home creates two fabrics on the device.
- "Turn On Pairing Mode" in the accessory's settings opens a window for a second controller with a one-time code. The label's code is dead once the device is paired.

## After an update

Home re-reads a device's details when its software version number goes up. We changed VendorName in an update that also raised the version from 1 to 2, and the Manufacturer row changed with no re-pair. Every earlier update left the version at 1 and changed nothing in Home.

Bump the version on every release.

Whether Home also notices endpoints added or removed is untested. Others report it keeping stale endpoints until the accessory is re-added, without saying whether they bumped the version.

## Firmware updates

From [Apple's Matter OTA User Guide](https://developer.apple.com/accessories/Apple-Matter-OTA-User-Guide.pdf), R5.

- The home hub is the OTA provider. It finds images through the Matter ledger.
- An uncertified accessory can be updated, from the test ledger only, with an Apple profile on the phone and every hub.
- Publishing to the test ledger needs a vendor account tied to a vendor ID. We use the shared test ID. We found no route for an individual.
- Apple says to allow 24 hours for the update to appear and 48 for it to apply.
- Developers report test-ledger updates showing up in Google Home and Home Assistant and never in Apple Home, [forum 812302](https://developer.apple.com/forums/thread/812302).

So `tools/matter-ota.sh` sends Matter updates itself, as a second controller beside Home. Home stays paired through an update and shows the new version afterwards.

## What HomeKit would fix

- Child lock as a property of the purifier.
- Names for the switches, supplied by the device.
- All five air quality levels.
- Possibly a filter reset. HomeKit has a characteristic for it. We have not checked that Home shows a button.

It costs QR-code Wi-Fi setup, which needs Apple's licensed accessory programme, and every controller that is not Apple's.

## Other sources

- [connectedhomeip: Matter on Apple devices](https://github.com/project-chip/connectedhomeip/blob/master/docs/guides/darwin.md). Device types decide the accessory category. Nothing about names.
- [Apple: Matter Accessory Best Practices](https://developer.apple.com/apple-home/downloads/Matter-Accessory-Best-Practices-for-Apple-Home.pdf). Thread and network diagnostics. Nothing about names or composition.
- [Apple forum 844646](https://developer.apple.com/forums/thread/844646). An Apple engineer: one accessory is what Home expects, and it ignores endpoints it does not support.
- [HomeKit air quality values](https://learn.microsoft.com/fil-ph/dotnet/api/homekit.hmcharacteristicvalueairquality?view=net-ios-26.0-10.0)
