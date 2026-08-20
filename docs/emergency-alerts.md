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

**Uninvolved people are not notified.** Someone with no role in an incident
learns about it through the ordinary record afterwards, not through an
interruption during. This is the rule that keeps the previous one usable: a
takeover screen only stays credible if it is rare and always meant for you.

## What each surface shows

| Surface | Shows |
| --- | --- |
| Public or shared displays | Safe public instructions only. Where to go, what not to do. No incident detail, no names, no internal state. |
| Authenticated staff devices | The role specific operational detail the holder is expected to act on, and nothing outside their role. |
| Command center | The full incident picture: every alert raised, who was asked, who accepted, who answered unable, what is still unassigned, and where the escalation chain currently stands. |

The split matters because a shared display has no idea who is standing in front
of it. It gets the public view, always.

## Local first

Each site eventually runs its own emergency controller. A site must be able to
raise, route, escalate and resolve a local alert with no wide area network and
with every central ME system unreachable. Central systems aggregate and inform.
They are not in the path of a local alarm.

Concretely, the local controller owns three things a network outage must not
take away:

1. **The roster.** Who is on site, in what role, right now. Held locally, so
   routing does not need to ask anything remote who the primary is.
2. **The routing and escalation rules.** Primary, backup, shift lead, incident
   commander, with their deadlines. Evaluated locally.
3. **The record.** What was raised, who answered what, and when. Written locally
   first and reconciled with central systems when the link returns, never the
   other way around.

A site that loses its link should notice nothing about how alerts behave.

## The manual panel

The long term local emergency controller has an offline manual human panel:
physical controls a person operates directly, that do not depend on Carl, on the
network, or on any agent being available or correct.

This is not a fallback feature to add later. It is the reason the rest of the
design can be trusted. An automated system that cannot be overridden by a person
standing next to it is a system that has to be right every time.

## Interaction with shift handovers

An alert raised near a shift change is the case that goes wrong most often. Two
rules apply, both borrowed from how ME intends to run incidents generally:

- An active incident is held jointly across the change, and the incoming lead
  explicitly accepts command. Until they do, the outgoing lead still has it.
- An alert already accepted by a person who is going off shift is reassigned
  deliberately, not silently reopened. Reassignment is an event with a name
  against it, not a timeout.

## What this means for ME OS

Nothing to build now. Three things to avoid designing away:

1. **A path to the display that does not go through the desktop.** A fullscreen
   takeover has to work even when the session is busy, locked or wedged.
2. **Identity that distinguishes a shared machine from a staffed one.** The
   public and staff views are different data, decided by which machine and which
   authenticated person, not by a setting.
3. **A local decision path.** Nothing in the eventual alert route should assume
   a remote service answers.

4. **A sound and haptic path that does not depend on a desktop session.** The
   same reasoning as the display: a machine that cannot make a noise when it
   must is not usable for this.

These are constraints on future design, not features. No milestone before the
game like ones after M11 touches any of them. The current milestones are M1
boot, M2 keyboard, M3 rectangle: the system cannot yet draw two things at once
on request, let alone take over a screen.
