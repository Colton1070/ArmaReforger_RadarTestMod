class AG0_ToggleRadarEmitter : ScriptedUserAction
{
	//------------------------------------------------------
	private int m_isRadarEmitterOn = false; 
	protected AG0_RadarRecieverTransmitterComponent radar = null;
	
	override void PerformAction(IEntity pOwnerEntity, IEntity pUserEntity)
	{
		if(!radar) {
	
			radar = AG0_RadarRecieverTransmitterComponent.Cast(pOwnerEntity.FindComponent(AG0_RadarRecieverTransmitterComponent));
		}
		if(!m_isRadarEmitterOn)
		{
			radar.SetEmitting(true);
			
			m_isRadarEmitterOn = true;
		}
		else {
			radar.SetEmitting(false);
			m_isRadarEmitterOn = false;
		}
		//pOwnerEntity.FindComponent(BaseHUDComponent);
	}
	
	override bool GetActionNameScript(out string outName)
	{
		if(m_isRadarEmitterOn)
		{
			outName = "Turn Off Radar Emitter";
		} else
		{
			outName = "Turn On Radar Emitter";
		}
		return true;
	}
}