//------------------------------------------------------------------------------------------------
//! Contact reports: when AI first spot something for the commander, raise a Game Master notification
//! carrying the position it was seen at.
//!
//! This rides the vanilla notification log rather than inventing a UI, which buys three things for
//! free: an entry in the commander's notification feed, click-to-teleport on that entry, and the
//! existing "EditorLastNotificationTeleport" keybind, which jumps the editor camera to the most
//! recent notification with a position. That keybind is the one-button-press jump - it is already in
//! the vanilla key bindings, so no new input action has to be registered.

//------------------------------------------------------------------------------------------------
//! Value is deliberately far above vanilla's ranges (currently up to 2xxx) so a future Bohemia
//! notification cannot collide with it.
modded enum ENotification
{
	OPC_CONTACT_SPOTTED = 30001,
}

//------------------------------------------------------------------------------------------------
//! UI info for the contact report. Name is the format string; %1 is filled with the contact's name.
class OPC_ContactNotificationUIInfo : SCR_UINotificationInfo
{
	//------------------------------------------------------------------------------------------------
	void OPC_ContactNotificationUIInfo()
	{
		SetName("Contact: %1");
	}

	//------------------------------------------------------------------------------------------------
	override ENotificationColor GetNotificationColor()
	{
		return ENotificationColor.WARNING;
	}

	//------------------------------------------------------------------------------------------------
	//! Never auto-set or update the position - see the note on OPC_ContactNotificationDisplayData.
	override ENotificationSetPositionData GetEditorSetPositionData()
	{
		return ENotificationSetPositionData.NEVER_AUTO_SET_POSITION;
	}
}

//------------------------------------------------------------------------------------------------
//! Display data for OPC_CONTACT_SPOTTED. param1 is the RplId of the contact's editable entity.
//!
//! Position handling is the important part. This class does not override SetPosition(), and its UI
//! info reports NEVER_AUTO_SET_POSITION, so the notification keeps the position it was sent with.
//! That is deliberate: a contact report is a snapshot of where the unit WAS seen. Letting the
//! position track the entity would give the commander a live feed of a unit that has since gone back
//! into the fog, which is the exact thing this mod exists to prevent.
class OPC_ContactNotificationDisplayData : SCR_NotificationDisplayData
{
	//------------------------------------------------------------------------------------------------
	void OPC_ContactNotificationDisplayData()
	{
		m_NotificationKey = ENotification.OPC_CONTACT_SPOTTED;
		m_info = new OPC_ContactNotificationUIInfo();
	}

	//------------------------------------------------------------------------------------------------
	override string GetText(SCR_NotificationData data)
	{
		int entityID;
		data.GetParams(entityID);

		string entityName;
		data.GetNotificationTextEntries(entityName);
		if (!GetEditableEntityName(entityID, entityName))
			return string.Empty;

		data.SetNotificationTextEntries(entityName);
		return super.GetText(data);
	}
}

//------------------------------------------------------------------------------------------------
//! Register the display data from script.
//! The vanilla lookup is built from Configs/Notifications/Notifications.conf. That is a shared
//! config, so overriding it to add one entry would clash with every other mod that adds a
//! notification - overriding the lookup instead keeps this mod free of config and prefab overrides.
[ComponentEditorProps(category: "GameScripted/Network", description: "")]
modded class SCR_NotificationsComponentClass : ScriptComponentClass
{
	protected ref OPC_ContactNotificationDisplayData m_OPC_ContactDisplayData;

	//------------------------------------------------------------------------------------------------
	override SCR_NotificationDisplayData GetNotificationDisplayData(ENotification notificationID)
	{
		if (notificationID != ENotification.OPC_CONTACT_SPOTTED)
			return super.GetNotificationDisplayData(notificationID);

		if (!m_OPC_ContactDisplayData)
			m_OPC_ContactDisplayData = new OPC_ContactNotificationDisplayData();

		return m_OPC_ContactDisplayData;
	}
}

//------------------------------------------------------------------------------------------------
//! Turns "these entities just became visible to the commander" into contact reports, with a
//! per-subject cooldown so a firefight does not bury the notification log.
//!
//! Infantry collapses to its AI group, so a spotted squad is one report rather than eight. Vehicles
//! always report on their own even when their crew already reported, because armour turning up is
//! its own piece of news.
//!
//! Entirely client-side and owned by OPC_FogOfWarClient.
class OPC_ContactReporter
{
	//! Seconds before the same subject can be reported again. 0 or less disables reporting.
	protected float m_fCooldown;

	//! RplId of the reported editable entity -> world time (seconds) it was last reported
	protected ref map<RplId, float> m_mLastReported = new map<RplId, float>();

	//--- Scratch
	protected ref array<SCR_EditableEntityComponent> m_aSubjects = {};

	//------------------------------------------------------------------------------------------------
	void SetCooldown(float seconds)
	{
		m_fCooldown = seconds;
	}

	//------------------------------------------------------------------------------------------------
	bool IsEnabled()
	{
		return m_fCooldown > 0;
	}

	//------------------------------------------------------------------------------------------------
	void Reset()
	{
		m_mLastReported.Clear();
	}

	//------------------------------------------------------------------------------------------------
	//! Report the entities that have just entered the commander's view.
	//! \param newlyRevealed Entities that were not revealed on the previous update
	//! \param primeOnly True to record the subjects as already reported without raising anything. Used
	//!        for the first update after subscribing, which carries everything currently in contact and
	//!        would otherwise dump the whole front line into the log at once.
	void ReportNewContacts(notnull set<IEntity> newlyRevealed, bool primeOnly)
	{
		if (m_fCooldown <= 0 || newlyRevealed.IsEmpty())
			return;

		float now = GetGame().GetWorld().GetWorldTime() * 0.001;
		Prune(now);

		//--- Collapse to reportable subjects, de-duplicated within this batch
		m_aSubjects.Clear();
		foreach (IEntity entity : newlyRevealed)
		{
			if (!entity)
				continue;

			SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.Cast(entity.FindComponent(SCR_EditableEntityComponent));
			if (!editable)
				continue;

			SCR_EditableEntityComponent subject = GetReportSubject(editable);
			if (subject && m_aSubjects.Find(subject) == -1)
				m_aSubjects.Insert(subject);
		}

		foreach (SCR_EditableEntityComponent subject : m_aSubjects)
		{
			RplId subjectId = Replication.FindItemId(subject);
			if (!subjectId.IsValid())
				continue;

			float lastReported;
			if (m_mLastReported.Find(subjectId, lastReported) && now - lastReported < m_fCooldown)
				continue;

			m_mLastReported.Set(subjectId, now);

			if (primeOnly)
				continue;

			vector position;
			if (!subject.GetPos(position) || position == vector.Zero)
				continue;

			SCR_NotificationsComponent.SendLocal(ENotification.OPC_CONTACT_SPOTTED, position, subjectId);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Infantry reports as its group; vehicles report as themselves.
	protected SCR_EditableEntityComponent GetReportSubject(notnull SCR_EditableEntityComponent entity)
	{
		EEditableEntityType type = entity.GetEntityType();

		if (type == EEditableEntityType.VEHICLE)
			return entity;

		if (type == EEditableEntityType.CHARACTER)
		{
			SCR_EditableEntityComponent group = entity.GetAIGroup();
			if (group)
				return group;
		}

		return entity;
	}

	//------------------------------------------------------------------------------------------------
	//! Drop entries that are past their cooldown. Keyed by RplId, so a destroyed entity's entry stays
	//! findable and removable.
	protected void Prune(float now)
	{
		for (int i = m_mLastReported.Count() - 1; i >= 0; i--)
		{
			if (now - m_mLastReported.GetElement(i) > m_fCooldown)
				m_mLastReported.RemoveElement(i);
		}
	}
}
