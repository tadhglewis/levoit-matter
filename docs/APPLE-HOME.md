# Apple Home and Matter: what we found

Notes on how Apple Home treats a Matter air purifier, gathered while building this firmware. Tested on iOS 27 with an Apple TV as home hub, against a Levoit Core 300S.

Each finding says where it comes from. "Observed" means we saw it on our own purifier, mostly by reading what Home sent in the firmware's log. "Reported" means other developers say so. Apple documents almost none of this.

## Summary

| Question | Answer | Basis |
|---|---|---|
| Does Home render an Air Purifier? | Yes: speed slider, Auto/Manual/Off, filter condition and life | Observed |
| Does Home render an Air Quality Sensor? | Yes: level and PM2.5 | Observed |
| Can the device name its own parts? | No | Observed and reported |
| Can the device set its default name? | No, always "Matter Accessory" when uncertified | Observed and reported |
| Can air quality show "Excellent"? | No | Observed |
| Can the filter be reset from Home? | No | Observed |
| Does the Identify button work? | Only if the endpoint declares an indicator | Observed in part |
| Can Home serve firmware updates? | Only through the Matter ledger | Apple's guide |
| Does Home notice changes after an update? | Yes for device details, if the software version number goes up | Observed |

## What renders

- **Air Purifier endpoint.** A tile with a speed slider, a mode menu with Auto, Manual and Off, Filter Condition and Filter Life.
- **Air Quality Sensor endpoint.** A tile with the air quality level and PM2.5 density.
- **On/Off Plug-in Unit endpoints.** Each becomes an outlet tile. In Australian English Home calls it "Power point".
- **Filter Life shows a bare number**, with no percent sign.
- **The accessory page says** "This accessory has not been certified to work with HomeKit", once, because the firmware uses the test vendor ID and the SDK's test attestation certificate.
- **Manufacturer and Model** rows show the Basic Information VendorName and ProductName. They are limited to 32 characters each.

## Naming

### The parts of a device cannot be named by the device

Three On/Off endpoints on the purifier arrive as "Power point", "Power point 2" and "Power point 3". The user has to rename them, and the names are lost on a re-pair.

| Mechanism | Result | Basis |
|---|---|---|
| Fixed Label cluster | Ignored | Reported: an Espressif contributor says Apple does not use it, and a developer who set it and verified it with chip-tool saw no change ([esp-matter #917](https://github.com/espressif/esp-matter/issues/917)) |
| User Label cluster | Ignored | Reported ([Apple forum thread 788551](https://developer.apple.com/forums/thread/788551)) |
| Descriptor TagList with a label on each tag | Ignored | Observed: we attached "Sleep", "Display" and "Child Lock" and re-paired. Also reported by the matterbridge-hass maintainer ([discussion #86](https://github.com/Luligu/matterbridge-hass/discussions/86)). An Apple engineer said tags are not a supported way to influence presentation ([thread 852549](https://developer.apple.com/forums/thread/852549)). |
| Bridged Device Basic Information NodeLabel, on bridged endpoints behind an Aggregator | Shown | Reported by two developers ([esp-matter #914](https://github.com/espressif/esp-matter/issues/914)). Not confirmed by Apple, and not tried here. |

One caveat on our TagList test: we confirmed the firmware attached the labels without error, but had no second controller to read them back.

Apple's engineers, in the threads we found, said that a single accessory is what Home expects, that endpoints it does not support are ignored ([thread 844646](https://developer.apple.com/forums/thread/844646)), and in one case only that a device lacked sufficient metadata ([thread 772367](https://developer.apple.com/forums/thread/772367)). None named a way to label parts.

### The bridge route, and why we did not take it

Making the purifier an Aggregator with the switches as bridged devices would probably get them named. We decided against it:

- The switches would become separate accessories, no longer grouped with the purifier.
- Home would list the purifier as a bridge, which it is not.
- We found no report of an Aggregator and an ordinary application endpoint working together on one node in Home, and one report of a thermostat failing in that arrangement ([connectedhomeip #30481](https://github.com/project-chip/connectedhomeip/issues/30481)).
- Root Basic Information NodeLabel is reported not to show as the bridge's name ([esp-matter #415](https://github.com/espressif/esp-matter/issues/415)).

### The default accessory name

At pairing Home offers "Matter Accessory" and asks for a name. Setting ProductName and VendorName did not change that. Other developers report the same after changing every name field ([esp-matter #246](https://github.com/espressif/esp-matter/issues/246)).

## What Home sends to the fan

Observed in the firmware's log, which records each write and which attribute it was.

| Action in Home | Writes |
|---|---|
| Tap Off | FanMode = Off |
| Tap Manual while off, no On/Off cluster | FanMode = High |
| Tap Auto while off, no On/Off cluster | FanMode = Auto and FanMode = High, about 40 ms apart, in either order |
| Tap Auto while off, with an On/Off cluster | On, then FanMode = Auto |
| Drag the slider | A stream of PercentSetting writes, several a second |
| Drag the slider to zero | PercentSetting = 0 |

- **Home has no Low, Medium or High control.** The mode menu only ever writes Off, Auto or High.
- **Home uses FanMode = High to mean "turn on"** when the purifier endpoint has no On/Off cluster. Tapping Auto from off therefore produces two conflicting mode writes, and because the order varies, obeying them in sequence sometimes leaves the purifier in manual.
- **With an On/Off cluster on the purifier endpoint, Home uses it for power.** Turning the purifier on and off from Home or Siri arrives as On and Off commands, and the FanMode = High writes stop. This is the fix for the conflicting pair above. Tapping Auto while off then arrives as On followed 100 ms later by FanMode = Auto, with no High.
- **The cluster must accept On and Toggle as well as Off.** esp-matter's `cluster::on_off::create` only registers Off. With only that, Home's On was refused as an unsupported command and Siri said "no response". Add `command::create_on` and `command::create_toggle`.
- **The slider sends raw percentages**, not steps. The firmware rounds to the nearest speed and reports the stepped value back, so the slider snaps.

## Air quality

Home has five levels and Matter has six, and Home matches them by name, not by position.

| Home | Matter |
|---|---|
| Excellent | no equivalent |
| Good | Good, Matter's best |
| Fair | Fair |
| Inferior | not confirmed |
| Poor | Poor |

So a purifier reporting its cleanest reading shows "Good", one step below the top of Home's scale, and an automation on "Excellent" never fires. We have only seen "Good" displayed. How Home places Moderate, Very Poor and Extremely Poor is unknown.

## Siri

- Siri has no notion of a purifier's sleep mode. "Turn on sleep mode on the air purifier" turned the purifier off.
- An extra On/Off endpoint is only reachable by voice through the name the user gives its tile, or through a scene.

## Filter

- Home shows HEPA Filter Monitoring's Condition as "Filter Life" and its ChangeIndication as "Filter Condition".
- Home offers no way to send the ResetCondition command. The purifier's own button does the reset.

## Identify

The naming screen during pairing has an Identify button. With the endpoint's IdentifyType left at the SDK default of None, it did nothing. We set it to VisibleIndicator and implemented the command. Whether the button now works is not yet confirmed.

## Pairing

- **Bluetooth pairing with Wi-Fi handover works** with the SDK's default path. The phone sends Wi-Fi credentials over Bluetooth.
- **Home creates two fabrics** on the device when it pairs.
- **"Turn On Pairing Mode"** in the accessory's settings opens a window for a second controller and shows a one-time code. The code on the label does not work once the device is paired.
- **Home re-reads a device's details when its software version number increases.** We changed VendorName in an update that also raised the version from 1 to 2, and the Manufacturer row changed without a re-pair. Earlier updates that kept the version at 1 changed nothing in Home. So every release has to bump the version.
- **Whether Home also notices endpoints being added or removed after a version bump is untested.** Developers report it keeping old endpoints until the accessory is re-added, without saying whether their update raised the version.

## Firmware updates

From [Apple's Matter OTA User Guide](https://developer.apple.com/accessories/Apple-Matter-OTA-User-Guide.pdf), release R5.

- The home hub acts as the Matter OTA provider. It finds images through the Matter Distributed Compliance Ledger.
- An uncertified accessory can be updated this way, but only from the test ledger, with an Apple profile installed on the phone and every hub.
- Publishing to the test ledger needs a vendor account tied to a vendor ID. We use the shared test ID, which nobody owns. We did not find a route for an individual.
- Apple tells developers to expect the update to appear within 24 hours and to be applied within 48.
- Developers report updates published to the test ledger appearing in Google Home and Home Assistant but not in Apple Home ([thread 812302](https://developer.apple.com/forums/thread/812302)).

This firmware uses its own authenticated HTTP update instead.

## What native HomeKit would give that Matter in Home does not

- Child lock as a property of the purifier, not a separate tile.
- Names supplied by the device for its switches.
- The full five-level air quality scale, including Excellent.
- A filter reset from Home.

It would cost QR-code Wi-Fi setup, which needs Apple's licensed accessory programme, and every non-Apple controller.

## Other sources

- [connectedhomeip: Matter development and testing on Apple devices](https://github.com/project-chip/connectedhomeip/blob/master/docs/guides/darwin.md). Device types drive accessory categories. It lists Air Purifier and Bridge as supported and says nothing about names.
- [Apple: Matter Accessory Best Practices for Apple Home](https://developer.apple.com/apple-home/downloads/Matter-Accessory-Best-Practices-for-Apple-Home.pdf). Covers Thread and network diagnostics. Nothing on naming or composition.
- [Apple HomeKit air quality values](https://learn.microsoft.com/fil-ph/dotnet/api/homekit.hmcharacteristicvalueairquality?view=net-ios-26.0-10.0)
- [Matter 1.4 standard namespaces](https://csa-iot.org/wp-content/uploads/2024/11/23-31936-003_matter-1-4-standard-namespaces.pdf)
