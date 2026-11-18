-- ============================================================================
-- VehicleController.lua
-- Controlador de Conducción para Vehículos con btRaycastVehicle de Bullet
-- ============================================================================

local Script = {}

-- Propiedades configurables desde el Inspector de SlipEngine
Script.maxEngineForce = 1800.0   -- Fuerza de aceleración en Newtons
Script.maxBrakeForce = 100.0     -- Fuerza de frenado normal
Script.handBrakeForce = 300.0    -- Fuerza de freno de mano (Espacio)
Script.maxSteeringAngle = 0.52   -- Ángulo máximo de giro de ruedas (radianes ~30°)
Script.steeringSpeed = 4.5       -- Velocidad de giro del volante

function Script:OnStart()
    self.currentSteering = 0.0
    self.vehicle = self.entity and self.entity:GetVehicle()
    if not self.vehicle then
        self.vehicle = self.entity and self.entity:GetComponent("Vehicle")
    end
end

function Script:OnUpdate(dt)
    if not self.vehicle then
        self.vehicle = self.entity and self.entity:GetVehicle()
        if not self.vehicle then return end
    end

    -- 1. Entrada de control (Ejes Analógicos / WASD / Flechas)
    local throttle = 0.0
    local steerInput = 0.0

    -- Soporte para SlipInputMap (ejes "Vertical" y "Horizontal")
    if Slip.Input and Slip.Input.GetAxis then
        throttle = Slip.Input.GetAxis("Vertical")
        steerInput = Slip.Input.GetAxis("Horizontal")
    end

    -- Fallback o lectura directa de teclas (admite Slip.Key o strings "W", "Up", etc.)
    if throttle == 0.0 and Slip.Input and Slip.Input.GetKey then
        local upKey = (Slip.Key and Slip.Key.W) or "W"
        local upArrow = (Slip.Key and Slip.Key.Up) or "Up"
        local downKey = (Slip.Key and Slip.Key.S) or "S"
        local downArrow = (Slip.Key and Slip.Key.Down) or "Down"

        if Slip.Input.GetKey(upKey) or Slip.Input.GetKey(upArrow) then
            throttle = 1.0
        elseif Slip.Input.GetKey(downKey) or Slip.Input.GetKey(downArrow) then
            throttle = -0.5 -- Reversa
        end
    end

    if steerInput == 0.0 and Slip.Input and Slip.Input.GetKey then
        local leftKey = (Slip.Key and Slip.Key.A) or "A"
        local leftArrow = (Slip.Key and Slip.Key.Left) or "Left"
        local rightKey = (Slip.Key and Slip.Key.D) or "D"
        local rightArrow = (Slip.Key and Slip.Key.Right) or "Right"

        if Slip.Input.GetKey(leftKey) or Slip.Input.GetKey(leftArrow) then
            steerInput = -1.0
        elseif Slip.Input.GetKey(rightKey) or Slip.Input.GetKey(rightArrow) then
            steerInput = 1.0
        end
    end

    local handbrake = false
    if Slip.Input and Slip.Input.GetAction then
        handbrake = Slip.Input.GetAction("Jump")
    end
    if not handbrake and Slip.Input and Slip.Input.GetKey then
        local spaceKey = (Slip.Key and Slip.Key.Space) or "Space"
        handbrake = Slip.Input.GetKey(spaceKey)
    end

    -- 2. Dirección suave hacia el ángulo objetivo
    local targetSteer = steerInput * self.maxSteeringAngle
    self.currentSteering = self.currentSteering + (targetSteer - self.currentSteering) * math.min(1.0, dt * self.steeringSpeed)
    self.vehicle:SetSteering(self.currentSteering)

    -- 3. Aceleración y Frenado
    if handbrake then
        self.vehicle:SetEngineForce(0.0)
        if self.vehicle.SetHandBrake then
            self.vehicle:SetHandBrake(self.handBrakeForce)
        else
            self.vehicle:SetBrake(self.handBrakeForce)
        end
    else
        self.vehicle:SetBrake(0.0)
        local force = throttle * self.maxEngineForce
        self.vehicle:SetEngineForce(force)
    end
end

return Script

