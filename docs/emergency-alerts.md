# Emergency alerts: design notes

**Nothing described here is implemented.** There is no alert code in this
repository, no alert hardware, and no schedule for building either. These are
design constraints written down early so the operating system does not grow in a
shape that makes them impossible later.

ME OS is currently at M2. Alerts are far past that.

## The problem with the obvious design

The obvious emergency system broadcasts every alert to everyone. It fails in
three ways. People who cannot act on an alert learn to ignore alerts. People who
can act cannot find the ones meant for them. And nobody knows who is handling
what, so either everyone responds or nobody does.

## Principles

**Not everyone, by default.** An alert goes to the people whose role makes them
able to act on it. Broadcasting to all is a deliberate choice for a specific
class of incident, not the default path.

**Actionable, not informational.** A recipient must be told why they in
particular were alerted and what action is expected of them. An alert that does
not answer both is a notification, and notifications do not get emergency
treatment.

**Answerable.** Every alert carries three responses:

| Response | Meaning |
| --- | --- |
| `ACCEPT` | I am handling this now |
| `NEED BACKUP` | I am on it and need help |
| `UNABLE` | I cannot take this, escalate |

**Escalation is a chain, not a retry.** Primary, then backup, then shift lead,
then incident commander. Each step is a different person with a different scope
of authority, and the chain advances on `UNABLE`, on no response within a
deadline, or on `NEED BACKUP`.

**Urgency earns intrusiveness.** A high urgency actionable alert may take over
the full screen, use a distinct sound, and vibrate where the device supports it.
Lower urgency alerts may not. Intrusiveness is tied to whether an action is
needed now, never to how important the sender feels.

## What each surface shows

| Surface | Shows |
| --- | --- |
| Public or shared displays | Safe public instructions only. Where to go, what not to do. No incident detail, no names, no internal state. |
| Authenticated staff devices | The role specific operational detail the holder is expected to act on, and nothing outside their role. |
| Command center | The full incident picture, including who was alerted, who accepted, and what is unassigned. |

The split matters because a shared display has no idea who is standing in front
of it. It gets the public view, always.

## Local first

Each site eventually runs its own emergency controller. A site must be able to
raise, route, escalate and resolve a local alert with no wide area network and
with every central ME system unreachable. Central systems aggregate and inform.
They are not in the path of a local alarm.

## The manual panel

The long term local emergency controller has an offline manual human panel:
physical controls a person operates directly, that do not depend on Carl, on the
network, or on any agent being available or correct.

This is not a fallback feature to add later. It is the reason the rest of the
design can be trusted. An automated system that cannot be overridden by a person
standing next to it is a system that has to be right every time.

## What this means for ME OS

Nothing to build now. Three things to avoid designing away:

1. **A path to the display that does not go through the desktop.** A fullscreen
   takeover has to work even when the session is busy, locked or wedged.
2. **Identity that distinguishes a shared machine from a staffed one.** The
   public and staff views are different data, decided by which machine and which
   authenticated person, not by a setting.
3. **A local decision path.** Nothing in the eventual alert route should assume
   a remote service answers.

These are constraints on future design, not features. No milestone before the
game like ones after M11 touches any of them.
