class AG0_RadarRecieverTransmitterComponentClass : ScriptComponentClass
{
}

//Requirements:
//Reciever transmitter should be able to recieve and/or transmit radar dependent on component config.
//Vehicles will be detected based on distance from radar taking into account distance from the ground.
//Ideally, a single line of sight check should suffice.

//! Determinates if the owner entity is covered by the radio signal eminating from the component marked as Source with the same encryption key
class AG0_RadarRecieverTransmitterComponent : ScriptComponent
{
    [Attribute("100", UIWidgets.EditBox, "Maximum range of the radar in meters")]
    protected float m_fMaxRange;
    
    [Attribute("1.0", UIWidgets.EditBox, "Radar strength (1.0 is standard)")]
    protected float m_fRadarStrength;
    
    [Attribute("0.1", UIWidgets.EditBox, "Base detection threshold")]
    protected float m_fBaseDetectionThreshold;
    
    [Attribute("0.75", UIWidgets.EditBox, "Detection range as fraction of max range (0.0 to 1.0)")]
    protected float m_fDetectionRangeFraction;
    
    [Attribute("1", "Auto-calculate threshold based on detection range")]
    protected bool m_bAutoCalculateThreshold;

    protected float m_fEffectiveDetectionThreshold;
    
    [Attribute("0.5", UIWidgets.EditBox, "Detection threshold (lower values make the radar more sensitive)")]
    protected float m_fDetectionThreshold;
    
    [Attribute("2", UIWidgets.ComboBox, "Radar mode: 0 = Emit only, 1 = Detect only, 2 = Both", "", ParamEnumArray.FromEnum(ERadarMode))]
    protected ERadarMode m_eRadarMode;

	[Attribute("360", UIWidgets.ComboBox, "Radar field of view in degrees", "", ParamEnumArray.FromEnum(ERadarFOV))]
    protected ERadarFOV m_eFieldOfView;
	
	[Attribute("1.0", UIWidgets.EditBox, "Base update interval in seconds")]
    protected float m_fBaseUpdateInterval;
	
	protected float m_fCurrentUpdateInterval;

	
    
    protected bool m_bIsEmitting;
    protected int m_iIFFKey;
    protected bool m_bIsPainted;
	protected float m_fPaintedAngle;
	protected float m_fPaintedStrength;
    
    protected ref array<IEntity> m_DetectedEntities;
	
	protected ref array<ref RadarSource> m_PaintingSources;
    protected ref array<ref RadarContact> m_DetectedContacts;
    protected ref array<ref RadarContact> m_DisplayableContacts;
	
	static const float SPEED_OF_LIGHT = 299792458; // meters per second
	static const float MIN_DISPLAY_DELAY = 0.1; // Minimum delay in seconds
    protected const float CONTACT_MEMORY_TIME = 30.0; // Time in seconds to remember a contact after losing it
	
    override void OnPostInit(IEntity owner)
    {
        super.OnPostInit(owner);
		
		
        m_DetectedEntities = new array<IEntity>();
        m_bIsEmitting = false;
        m_bIsPainted = false;
        m_iIFFKey = -1;
        m_fPaintedAngle = 0.0;
        m_fPaintedStrength = 0.0;

		if (m_bAutoCalculateThreshold)
        {
            CalculateEffectiveThreshold();
        }
        else
        {
            m_fEffectiveDetectionThreshold = m_fBaseDetectionThreshold;
        }
		
        Print("RadarComponent initialized on " + owner.GetName() + ". Mode: " + typename.EnumToString(ERadarMode, m_eRadarMode));

		AG0_RadarCoverageSystem radarSystem = AG0_RadarCoverageSystem.GetInstance();
	
	    if (!radarSystem)
	        return;
	
	    radarSystem.RegisterRadarComponent(this);
	
	    SetEventMask(owner, EntityEvent.INIT);
		
		m_DetectedContacts = new array<ref RadarContact>();
        m_DisplayableContacts = new array<ref RadarContact>();

        UpdateRadarSettings();
    }
	
	void SetFieldOfView(ERadarFOV fov)
    {
        if (m_eFieldOfView != fov)
        {
            m_eFieldOfView = fov;
            UpdateRadarSettings();
            Print("Radar field of view changed to: " + FOVToString(m_eFieldOfView));
        }
    }
	
	ERadarFOV GetFieldOfView()
    {
        return m_eFieldOfView;
    }
	
	protected void UpdateRadarSettings()
    {
        // Calculate update interval based on FOV
        float fovMultiplier = 360.0 / FOVToFloat(m_eFieldOfView);
        m_fCurrentUpdateInterval = m_fBaseUpdateInterval / fovMultiplier;

        // Remove any existing update callback
        GetGame().GetCallqueue().Remove(UpdateContacts);

        // Set up new update callback
        GetGame().GetCallqueue().CallLater(UpdateContacts, m_fCurrentUpdateInterval * 1000, true);

        Print("Radar update interval set to: " + m_fCurrentUpdateInterval + " seconds");
    }
	
	void AddDetectedEntity(IEntity entity, vector position)
    {
        float azimuth = CalculateAzimuth(position);
        float elevation = CalculateElevation(position);
        
        RadarContact existingContact = FindContact(entity);
        if (!existingContact)
        {
            RadarContact newContact = new RadarContact(entity, position, azimuth, elevation);
            m_DetectedContacts.Insert(newContact);
        }
        else
        {
            existingContact.UpdateDetection(position, azimuth, elevation);
        }
    }
	
	protected RadarContact FindContact(IEntity entity)
    {
        foreach (RadarContact contact : m_DetectedContacts)
        {
            if (contact.GetEntity() == entity)
                return contact;
        }
        return null;
    }
	
	protected void UpdateContacts()
    {
        float currentTime = System.GetTickCount() / 1000.0;
        vector ownerOrientation = GetOwner().GetAngles();

        for (int i = m_DetectedContacts.Count() - 1; i >= 0; i--)
        {
            RadarContact contact = m_DetectedContacts[i];
            
            if (currentTime - contact.GetLastDetectedTime() > CONTACT_MEMORY_TIME)
            {
                m_DetectedContacts.RemoveOrdered(i);
                m_DisplayableContacts.RemoveItem(contact);
                continue;
            }
            
            if (IsEntityInFOV(contact.GetEntity(), ownerOrientation))
            {
                if (!contact.IsDisplayable() && contact.ShouldDisplay(currentTime))
                {
                    contact.SetDisplayable(true);
                    m_DisplayableContacts.Insert(contact);
                }
                
                if (contact.ShouldUpdate(currentTime, m_fCurrentUpdateInterval))
                {
                    UpdateContactPosition(contact);
                }
            }
        }
    }
	
	array<ref RadarContact> GetDisplayableContacts()
    {
        return m_DisplayableContacts;
    }
	
	bool IsEntityInFOV(IEntity entity, vector ownerOrientation)
    {
        vector directionToEntity = entity.GetOrigin() - GetOwner().GetOrigin();
        directionToEntity.Normalize();
        
        vector forwardVector = ownerOrientation;
        forwardVector.Normalize();
        
        float angle = Math.Acos(vector.Dot(forwardVector, directionToEntity)) * Math.RAD2DEG;
        float halfFOV = FOVToFloat(m_eFieldOfView) / 2;
        
        return angle <= halfFOV;
    }
	
	void UpdateContactPosition(RadarContact contact)
    {
        IEntity entity = contact.GetEntity();
        vector entityPos = entity.GetOrigin();
        vector radarPos = GetOwner().GetOrigin();
        
        vector relativePosition = entityPos - radarPos;
        float azimuth = CalculateAzimuth(relativePosition);
        float elevation = CalculateElevation(relativePosition);
        
        contact.UpdateDetection(relativePosition, azimuth, elevation);
        contact.MarkAsUpdated();
    }

	float CalculateAzimuth(vector relativePosition)
    {
        vector horizontalPos = Vector(relativePosition[0], 0, relativePosition[2]);
        vector forwardVector = GetOwner().GetTransformAxis(2);
        forwardVector[1] = 0; // Project to horizontal plane
        forwardVector.Normalize();
        
        float angle = Math.Acos(vector.Dot(forwardVector, horizontalPos.Normalized())) * Math.RAD2DEG;
        
        // Determine if the angle is left or right of forward
        if (vector.Dot(GetOwner().GetTransformAxis(0), horizontalPos) < 0)
        {
            angle = 360 - angle;
        }
        
        return angle;
    }
	
	float CalculateElevation(vector relativePosition)
    {
        vector horizontalPos = Vector(relativePosition[0], 0, relativePosition[2]);
        float horizontalDistance = horizontalPos.Length();
        
        return Math.Atan2(relativePosition[1], horizontalDistance) * Math.RAD2DEG;
    }
	
	static float FOVToFloat(ERadarFOV fov)
    {
        switch (fov)
        {
            case ERadarFOV.FOV_30: return 30.0;
            case ERadarFOV.FOV_90: return 90.0;
            case ERadarFOV.FOV_360: return 360.0;
        }
        return 360.0; // Default
    }
	
	static string FOVToString(ERadarFOV fov)
    {
        switch (fov)
        {
            case ERadarFOV.FOV_30: return "30 degrees";
            case ERadarFOV.FOV_90: return "90 degrees";
            case ERadarFOV.FOV_360: return "360 degrees";
        }
        return "Unknown";
    }
	
	void CalculateEffectiveThreshold()
    {
        float detectionRange = m_fMaxRange * m_fDetectionRangeFraction;
        m_fEffectiveDetectionThreshold = m_fRadarStrength / (detectionRange * detectionRange);
        
        Print("Auto-calculated detection threshold: " + m_fEffectiveDetectionThreshold + 
              " (Max Range: " + m_fMaxRange + 
              ", Detection Range: " + detectionRange + 
              ", Radar Strength: " + m_fRadarStrength + ")");
    }
	
	bool CanEmit()
    {
        bool canEmit = (m_eRadarMode == ERadarMode.EMIT_ONLY || m_eRadarMode == ERadarMode.EMIT_AND_DETECT);
        Print("CanEmit checked for " + GetOwner().GetName() + ": " + canEmit);
        return canEmit;
    }
	
	bool CanDetect()
    {
        bool canDetect = (m_eRadarMode == ERadarMode.DETECT_ONLY || m_eRadarMode == ERadarMode.EMIT_AND_DETECT);
        Print("CanDetect checked for " + GetOwner().GetName() + ": " + canDetect);
        return canDetect;
    }
    
    void SetEmitting(bool isEmitting)
    {
		if (CanEmit())
        {
            m_bIsEmitting = isEmitting;
            Print("Radar emission set to: " + m_bIsEmitting + " on " + GetOwner().GetName());
        }
        else
        {
            Print("Cannot set emitting on " + GetOwner().GetName() + ". Current mode: " + typename.EnumToString(ERadarMode, m_eRadarMode));
        }
    }
    
    bool IsEmitting()
    {
        bool canEmit = (m_eRadarMode == ERadarMode.EMIT_ONLY || m_eRadarMode == ERadarMode.EMIT_AND_DETECT);
        return m_bIsEmitting && canEmit;
    }
    
    void SetIFFKey(int key)
    {
        m_iIFFKey = key;
    }
    
    int GetIFFKey()
    {
        return m_iIFFKey;
    }
	
	void AddDetectedEntity(IEntity entity)
	{
	    if (!m_DetectedEntities.Contains(entity))
	        m_DetectedEntities.Insert(entity);
	}
    
    bool IsInLineOfSight(IEntity target)
    {
        vector startPos = GetOwner().GetOrigin();
        vector endPos = target.GetOrigin();
        vector direction = endPos - startPos;
        float distance = direction.Length();
		Print("Distance between radar and target: " + distance);
        direction.Normalize();
        
        BaseWorld world = GetGame().GetWorld();
        
        TraceParam trace = new TraceParam();
        trace.Start = startPos;
        trace.End = startPos + direction * distance;
        trace.Flags = TraceFlags.WORLD | TraceFlags.ENTS;
        trace.LayerMask = TRACE_LAYER_CAMERA;
        trace.Exclude = GetOwner();
        
        if (startPos[1] > world.GetOceanBaseHeight())
            trace.Flags = trace.Flags | TraceFlags.OCEAN;
        
         float traceScale = GetGame().GetWorld().TraceMove(trace, null);
        
        Print("Trace result for " + GetOwner().GetName() + " to " + target.GetName() + ": " + traceScale);

        // If traceScale is very close to 1 (allowing for floating-point imprecision, and collision with model components),
        // or if the difference between full distance and traced distance is very small,
        // we consider it a clear line of sight
        const float EPSILON = 0.035; // Adjust this value as needed
        if (traceScale >= (1 - EPSILON) || (distance * (1 - traceScale)) < EPSILON)
        {
            Print("Clear line of sight detected");
            return true;
        }
        else
        {
            Print("Line of sight obstructed at " + (traceScale * 100) + "% of the distance");
            return false;
        }
    }
	
	float CalculateRelativeAngleTo(IEntity target)
    {
        vector sourcePos = GetOwner().GetOrigin();
        vector targetPos = target.GetOrigin();
        
        // Calculate direction vector in 2D (ignoring vertical difference)
        vector direction = targetPos - sourcePos;
        direction[1] = 0; // Set Y (vertical) component to 0
        direction.Normalize();
        
        // Get the forward and right vectors of our vehicle
        vector mat[4];
        GetOwner().GetTransform(mat);
        vector forwardVec = mat[2];
        vector rightVec = mat[0];
        forwardVec[1] = 0; // Ignore vertical component
        rightVec[1] = 0;
        forwardVec.Normalize();
        rightVec.Normalize();
        
        // Calculate dot products
        float forwardDot = vector.Dot(forwardVec, direction);
        float rightDot = vector.Dot(rightVec, direction);
        
        // Calculate angle using atan2
        float angle = Math.Atan2(rightDot, forwardDot);
        
        // Convert to degrees and normalize to 0-360 range
        angle = Math.RAD2DEG * angle;
        if (angle < 0)
            angle += 360;
        
        return angle;
    }
    
    float CalculateRelativeAngleFrom(IEntity target)
    {
        vector sourcePos = GetOwner().GetOrigin();
        vector targetPos = target.GetOrigin();
        
        // Calculate direction vector in 2D (ignoring vertical difference)
        vector direction = sourcePos - targetPos; // Note: This is reversed from CalculateRelativeAngleTo
        direction[1] = 0; // Set Y (vertical) component to 0
        direction.Normalize();
        
        // Get the forward and right vectors of the target vehicle
        vector mat[4];
        target.GetTransform(mat);
        vector forwardVec = mat[2];
        vector rightVec = mat[0];
        forwardVec[1] = 0; // Ignore vertical component
        rightVec[1] = 0;
        forwardVec.Normalize();
        rightVec.Normalize();
        
        // Calculate dot products
        float forwardDot = vector.Dot(forwardVec, direction);
        float rightDot = vector.Dot(rightVec, direction);
        
        // Calculate angle using atan2
        float angle = Math.Atan2(rightDot, forwardDot);
        
        // Convert to degrees and normalize to 0-360 range
        angle = Math.RAD2DEG * angle;
        if (angle < 0)
            angle += 360;
        
        return angle;
    }
	
	float CalculateDetectionStrength(float distance)
    {
        // Prevent division by zero
        if (distance < 0.01) // Use a small threshold to avoid extremely large values
        {
            Print("Warning: Very small distance detected. Using minimum distance of 0.01");
            distance = 0.01;
        }

        //inverse square law for detection
        float strength = m_fRadarStrength / (distance * distance);
        Print("Detection strength calculated: " + strength + " (Distance: " + distance + ", Radar Strength: " + m_fRadarStrength + ")");
        return strength;
    }
	
    
    void SetPainted(bool isPainted, float angle = 0, float strength = 0, int key = -1)
    {
        if (CanDetect())
        {
            m_bIsPainted = isPainted;
            if (isPainted)
            {
                m_fPaintedAngle = angle;
                m_fPaintedStrength = strength;
                Print(GetOwner().GetName() + " painted by radar. Angle: " + angle + ", Strength: " + strength + " Radar IFF: " + key);
            }
            else
            {
                Print(GetOwner().GetName() + " no longer painted by radar");
            }
        }
        else
        {
            Print("Cannot set painted on " + GetOwner().GetName() + ". Current mode: " + typename.EnumToString(ERadarMode, m_eRadarMode));
        }
    }
	
	bool IsPainted(out float angle, out float strength)
    {
       	angle = m_fPaintedAngle;
        strength = m_fPaintedStrength;
        Print("Painted status checked for " + GetOwner().GetName() + ". Is Painted: " + m_bIsPainted + ", Angle: " + angle + ", Strength: " + strength);
        return m_bIsPainted;
    }
    
    array<IEntity> GetDetectedEntities()
    {
        return m_DetectedEntities;
    }
	
	float GetMaxRange()
	{
	    return m_fMaxRange;
	}
	
	float GetEffectiveDetectionThreshold()
	{
	    return m_fEffectiveDetectionThreshold;
	}
	
	void ~AG0_RadarRecieverTransmitterComponent()
	{
	    AG0_RadarCoverageSystem radarSystem = AG0_RadarCoverageSystem.GetInstance();
	
	    if (radarSystem)
	        radarSystem.UnregisterRadarComponent(this);
	
	    // Any other cleanup you need
	}
}

class RadarSource
{
    protected AG0_RadarRecieverTransmitterComponent m_SourceRadar;
    protected float m_fRelativeDirection;
    protected float m_fStrength;

    void RadarSource(AG0_RadarRecieverTransmitterComponent sourceRadar)
    {
        m_SourceRadar = sourceRadar;
    }

    void Update(float direction, float strength)
    {
        m_fRelativeDirection = direction;
        m_fStrength = strength;
    }

    AG0_RadarRecieverTransmitterComponent GetSourceRadar()
    {
        return m_SourceRadar;
    }

    float GetRelativeDirection()
    {
        return m_fRelativeDirection;
    }

    float GetStrength()
    {
        return m_fStrength;
    }
}

class RadarContact
{
    protected IEntity m_Entity;
    protected vector m_vPosition;      // Relative 3D position from radar
    protected float m_fDistance;       // Distance from radar
	protected float m_fAngle;
    protected float m_fAzimuth;        // Horizontal angle (0-360 degrees)
    protected float m_fElevation;      // Vertical angle (-90 to 90 degrees)
    protected float m_fLastDetectedTime;
    protected float m_fLastUpdateTime;
    protected bool m_bIsDisplayable;
    protected float m_fDisplayTime;

    void RadarContact(IEntity entity, vector position, float azimuth, float elevation)
    {
        m_Entity = entity;
        UpdateDetection(position, azimuth, elevation);
        m_bIsDisplayable = false;
        m_fLastUpdateTime = System.GetTickCount() / 1000.0;
    }

    void UpdateDetection(vector position, float azimuth, float elevation)
    {
        m_fLastDetectedTime = System.GetTickCount() / 1000.0;
        m_vPosition = position;
        m_fDistance = position.Length();
        m_fAzimuth = azimuth;
		m_fAngle = azimuth;
        m_fElevation = elevation;
        
        // Calculate display time based on radar wave travel time
        float travelTime = (2 * m_fDistance) / AG0_RadarRecieverTransmitterComponent.SPEED_OF_LIGHT;
        m_fDisplayTime = m_fLastDetectedTime + Math.Max(travelTime, AG0_RadarRecieverTransmitterComponent.MIN_DISPLAY_DELAY);
    }

    vector GetPosition()
    {
        return m_vPosition;
    }

    float GetDistance()
    {
        return m_fDistance;
    }
	
	float GetAngle()
    {
        return m_fAngle;
    }

    float GetAzimuth()
    {
        return m_fAzimuth;
    }

    float GetElevation()
    {
        return m_fElevation;
    }

    IEntity GetEntity()
    {
        return m_Entity;
    }

    float GetLastDetectedTime()
    {
        return m_fLastDetectedTime;
    }

    bool ShouldDisplay(float currentTime)
    {
        return currentTime >= m_fDisplayTime;
    }

    void SetDisplayable(bool displayable)
    {
        m_bIsDisplayable = displayable;
    }

    bool IsDisplayable()
    {
        return m_bIsDisplayable;
    }

    void MarkAsUpdated()
    {
        m_fLastUpdateTime = System.GetTickCount() / 1000.0;
    }

    bool ShouldUpdate(float currentTime, float updateInterval)
    {
        return (currentTime - m_fLastUpdateTime) >= updateInterval;
    }
}

enum ERadarMode {
    EMIT_ONLY,
    DETECT_ONLY,
    EMIT_AND_DETECT
}

enum ERadarFOV {
    FOV_30=30,
    FOV_90=90,
    FOV_360=360
}