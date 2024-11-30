//Requirements:
//We need to create a game system so that we can calculate if vehicles are near radar emplacements.
//This should be much cheaper than having each component doing it's own checks
//We could do each ray cast from each component or do it here, here might be better so we only have to transfer info related to contacts.

class AG0_RadarCoverageSystem : GameSystem
{
    protected ref array<AG0_RadarRecieverTransmitterComponent> m_aRadarComponents = {};
    protected ref array<IEntity> m_aVehicles = {};
    
    protected static const float UPDATE_INTERVAL = 1.0; // Update every 1 second
    
    //------------------------------------------------------------------------------------------------
	
	protected static AG0_RadarCoverageSystem s_Instance;

	
	static AG0_RadarCoverageSystem GetInstance()
	{
		World world = GetGame().GetWorld();

		if (!world)
			return null;

		return AG0_RadarCoverageSystem.Cast(world.FindSystem(AG0_RadarCoverageSystem));
	}

    
    //------------------------------------------------------------------------------------------------
    override void OnStarted()
	{
	    super.OnStarted();
	    Print("AG0_RadarCoverageSystem OnStarted called");
	    GetGame().GetCallqueue().CallLater(UpdateRadarCoverage, UPDATE_INTERVAL * 1000, true);
	}
    
    //------------------------------------------------------------------------------------------------
    override void OnStopped()
    {
        super.OnStopped();
        GetGame().GetCallqueue().Remove(UpdateRadarCoverage);
        s_Instance = null;
    }
    
    //------------------------------------------------------------------------------------------------
    void RegisterRadarComponent(AG0_RadarRecieverTransmitterComponent component)
    {
        if (!m_aRadarComponents.Contains(component))
            m_aRadarComponents.Insert(component);
    }
    
    //------------------------------------------------------------------------------------------------
    void UnregisterRadarComponent(AG0_RadarRecieverTransmitterComponent component)
    {
        m_aRadarComponents.RemoveItem(component);
    }
    
    //------------------------------------------------------------------------------------------------
    protected void UpdateRadarCoverage()
    {
        Print("UpdateRadarCoverage called. Active radar components: " + m_aRadarComponents.Count());
        
        UpdateVehicleList();
        Print("Vehicles updated. Total vehicles: " + m_aVehicles.Count());
        
        foreach (AG0_RadarRecieverTransmitterComponent radar : m_aRadarComponents)
	    {
	        if (radar.IsEmitting())
	        {
	            vector radarPos = radar.GetOwner().GetOrigin();
	            vector ownerOrientation = radar.GetOwner().GetAngles();
	            float maxRange = radar.GetMaxRange();
	            
	            foreach (IEntity vehicle : m_aVehicles)
	            {
	                vector vehiclePos = vehicle.GetOrigin();
	                vector relativePos = vehiclePos - radarPos;
	                float distance = relativePos.Length();
	                
	                if (distance < 0.01)
	                {
	                    Print("Warning: Very small distance detected between radar and vehicle: " + distance);
	                    continue; // Skip this vehicle to avoid potential issues
	                }
	
	                if (radar.IsEntityInFOV(vehicle, ownerOrientation))
	                {
	                    if (distance <= maxRange && radar.IsInLineOfSight(vehicle))
	                    {
	                        float detectionStrength = radar.CalculateDetectionStrength(distance);
	                        if (detectionStrength > radar.GetEffectiveDetectionThreshold())
	                        {
	                            radar.AddDetectedEntity(vehicle, relativePos);
	                            float angle = radar.CalculateRelativeAngleTo(vehicle);
	                            NotifyDetectedEntity(vehicle, angle, detectionStrength, radar.GetIFFKey());
	                        }
	                    }
	                }
	            }
	        }
	    }
    }
    
    //------------------------------------------------------------------------------------------------
    protected void UpdateVehicleList()
    {
        m_aVehicles.Clear();
        
        SCR_EditableEntityCore core = SCR_EditableEntityCore.Cast(SCR_EditableEntityCore.GetInstance(SCR_EditableEntityCore));
        if (!core)
            return;
        
        set<SCR_EditableEntityComponent> entities = new set<SCR_EditableEntityComponent>;
        core.GetAllEntities(entities, true, false);
        
        foreach (SCR_EditableEntityComponent ent : entities)
        {
            Vehicle vehicle = Vehicle.Cast(ent.GetOwner());
            if (vehicle && !ent.IsDestroyed())
                m_aVehicles.Insert(vehicle);
        }
    }
    
    //------------------------------------------------------------------------------------------------
    protected void NotifyDetectedEntity(IEntity entity, float angle, float strength, int key)
    {
		Print("Notifying detected entity: " + entity.GetName() + " (Angle: " + angle + ", Strength: " + strength + ")");
        AG0_RadarRecieverTransmitterComponent radarComp = AG0_RadarRecieverTransmitterComponent.Cast(entity.FindComponent(AG0_RadarRecieverTransmitterComponent));
        if (radarComp && radarComp.CanDetect())
        {
            radarComp.SetPainted(true, angle, strength, key);
        }
    }
}