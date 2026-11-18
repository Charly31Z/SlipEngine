-- ============================================================================
-- NFSCarController.lua
-- Controlador de Conducción Deportiva estilo Need for Speed / Forza Horizon
-- Integración con btRaycastVehicle, Derrape Moderno, Nitro, Caja de 6 Velocidades y HUD
-- ============================================================================

local Script = {}

-- ----------------------------------------------------------------------------
-- Propiedades configurables desde el Inspector de SlipEngine
-- ----------------------------------------------------------------------------
-- Motor y Potencia
Script.maxEngineForce = 2600.0          -- Fuerza de aceleración del motor (Newtons)
Script.nitroMultiplier = 1.70           -- Multiplicador de empuje con Nitro
Script.nitroImpulse = 480.0             -- Impulso de aceleración continua para empuje inmediato
Script.maxBrakeForce = 60.0             -- Fuerza de frenado calibrada para detenerse rápido sin bloquear dirección
Script.handBrakeForce = 5.5             -- Fuerza de freno de mano arcade suave para romper tracción sin bloquear en seco
Script.maxSteeringAngle = 0.50          -- Ángulo máximo de giro de ruedas base (~28 grados)
Script.steeringSpeed = 6.5              -- Rapidez de respuesta de giro del volante (ágil a cualquier velocidad)

-- Físicas de Derrape Moderno (Need for Speed / Forza Horizon)
Script.baseTireGrip = 5.2               -- Fricción lateral base en seco (agarre firme)
Script.driftRearGrip = 3.4              -- Fricción en ruedas traseras inicial para derrape fluido
Script.driftFrontGrip = 5.4             -- Fricción delantera para dirigir y traccionar durante el drift
Script.driftSteerAssist = 0.50          -- Asistencia de contravolante estabilizadora anti-trompo

-- Calibración de Suspensión Deportiva Firme (Sin cabeceo, sin hundimiento y sin tocar el suelo)
Script.suspensionStiffness = 45.0       -- Rigidez calculada para chasis deportivo
Script.suspensionDamping = 6.5          -- Amortiguación crítica de rebote
Script.suspensionCompression = 5.2      -- Amortiguación de compresión anti-hundimiento
Script.maxSuspensionTravelCm = 18.0     -- Recorrido de suspensión controlado (18 cm)
Script.rollInfluence = 0.10             -- Balance de inclinación deportiva estable (con estabilizador antivuelco activo)

-- Configuración de Nombres de Entidades HUD
Script.speedTextName = "SpeedText"
Script.gearTextName = "GearText"
Script.rpmTextName = "RPMText"
Script.nitroBarName = "NitroBar"
Script.cameraName = "MainCamera"

-- ----------------------------------------------------------------------------
-- Ciclo de Vida: Inicialización
-- ----------------------------------------------------------------------------
function Script:OnStart()
    self.currentSteering = 0.0
    self.isDrifting = false
    self.driftDirection = 0.0
    self.nitroActive = false
    self.nitroFuel = 1.0                -- Tanque lleno al 100% (0.0 a 1.0)
    self.nitroRechargeRate = 0.08       -- Recarga por segundo derrapando
    self.nitroConsumeRate = 0.28        -- Consumo por segundo al usar nitro

    -- Sistema de Transmisión
    self.currentGear = 1                -- 1 a 6, 0 = N, -1 = R
    self.currentRPM = 900.0             -- Ralentí base
    self.targetRPM = 900.0
    self.idleRPM = 900.0
    self.redlineRPM = 7200.0
    self.maxRPM = 7600.0
    self.shiftTimer = 0.0
    self.shiftDuration = 0.16           -- Pausa de torque durante cambio de marcha

    -- Relaciones de velocidad máxima por marcha (km/h)
    self.gearSpeeds = {
        [1] = 48.0,
        [2] = 86.0,
        [3] = 132.0,
        [4] = 178.0,
        [5] = 228.0,
        [6] = 290.0
    }

    -- 1. Obtener componentes
    self.vehicle = (self.entity and self.entity:GetVehicle()) or (self.entity and self.entity:GetComponent("Vehicle"))
    self.rigidBody = (self.entity and self.entity:GetRigidBody()) or (self.entity and self.entity:GetComponent("RigidBody"))
    self.transform = (self.entity and self.entity:GetTransform()) or (self.entity and self.entity:GetComponent("Transform"))

    -- 2. Calibrar suspensión en Bullet
    if self.vehicle then
        self.vehicle.suspensionStiffness = self.suspensionStiffness
        self.vehicle.suspensionDamping = self.suspensionDamping
        self.vehicle.suspensionCompression = self.suspensionCompression
        self.vehicle.maxSuspensionForce = 35000.0
        self.vehicle.maxSuspensionTravelCm = self.maxSuspensionTravelCm
        self.vehicle.frictionSlip = self.baseTireGrip
        self.vehicle.rollInfluence = self.rollInfluence
        self.vehicle.showDebugGizmos = true
        self.vehicle.enableAntiRollBar = true
        self.vehicle.antiRollStrength = 5500.0
    end

    if self.rigidBody then
        self.rigidBody:SetFriction(0.4)
        self.rigidBody.angularDrag = 0.35
    end

    -- 3. Localizar HUD
    self:FindHUDEntities()

    -- 4. Bloquear y ocultar cursor
    if Slip.Input and Slip.Input.SetCursorLocked then
        Slip.Input.SetCursorLocked(true)
        Slip.Input.SetCursorVisible(false)
    end
end

function Script:FindHUDEntities()
    if Slip.Scene and Slip.Scene.FindEntity then
        self.speedTextEnt = Slip.Scene.FindEntity(self.speedTextName)
        self.gearTextEnt = Slip.Scene.FindEntity(self.gearTextName)
        self.rpmTextEnt = Slip.Scene.FindEntity(self.rpmTextName)
        self.nitroBarEnt = Slip.Scene.FindEntity(self.nitroBarName)
        self.camEnt = Slip.Scene.FindEntity(self.cameraName)

        -- Búsqueda en los hijos de HUD_Canvas si están anidados
        local canvas = Slip.Scene.FindEntity("HUD_Canvas")
        if canvas then
            if not self.speedTextEnt and canvas.FindChild then
                self.speedTextEnt = canvas:FindChild(self.speedTextName, true)
            end
            if not self.gearTextEnt and canvas.FindChild then
                self.gearTextEnt = canvas:FindChild(self.gearTextName, true)
            end
            if not self.rpmTextEnt and canvas.FindChild then
                self.rpmTextEnt = canvas:FindChild(self.rpmTextName, true)
            end
            if not self.nitroBarEnt and canvas.FindChild then
                self.nitroBarEnt = canvas:FindChild(self.nitroBarName, true)
            end
        end
    end
end

-- ----------------------------------------------------------------------------
-- Ciclo de Vida: Actualización Cuadro a Cuadro
-- ----------------------------------------------------------------------------
function Script:OnUpdate(dt)
    if not self.vehicle then
        self.vehicle = (self.entity and self.entity:GetVehicle()) or (self.entity and self.entity:GetComponent("Vehicle"))
        if not self.vehicle then return end
    end
    if not self.transform then
        self.transform = (self.entity and self.entity:GetTransform()) or (self.entity and self.entity:GetComponent("Transform"))
        if not self.transform then return end
    end

    local currentSpeedKmH = self.vehicle:GetSpeed() or 0.0
    local absSpeed = math.abs(currentSpeedKmH)

    -- 1. ENTRADA DEL JUGADOR
    local throttle = 0.0
    local steerInput = 0.0

    if Slip.Input and Slip.Input.GetAxis then
        throttle = Slip.Input.GetAxis("Vertical")
        steerInput = -Slip.Input.GetAxis("Horizontal")
    end

    if throttle == 0.0 and Slip.Input and Slip.Input.GetKey then
        if Slip.Input.GetKey("W") or Slip.Input.GetKey("Up") then
            throttle = 1.0
        elseif Slip.Input.GetKey("S") or Slip.Input.GetKey("Down") then
            throttle = -1.0
        end
    end

    if steerInput == 0.0 and Slip.Input and Slip.Input.GetKey then
        if Slip.Input.GetKey("A") or Slip.Input.GetKey("Left") then
            steerInput = 1.0
        elseif Slip.Input.GetKey("D") or Slip.Input.GetKey("Right") then
            steerInput = -1.0
        end
    end

    local handbrake = false
    if Slip.Input and Slip.Input.GetKey then
        handbrake = Slip.Input.GetKey("Space")
    end

    local nitroKey = false
    if Slip.Input and Slip.Input.GetKey then
        nitroKey = Slip.Input.GetKey("LeftShift") or Slip.Input.GetKey("Shift") or Slip.Input.GetKey("N")
    end

    -- Alternar Gizmos de Debug con la tecla F3 o G
    if not self.gizmoCooldown then self.gizmoCooldown = 0.0 end
    self.gizmoCooldown = self.gizmoCooldown - dt
    if Slip.Input and Slip.Input.GetKey and (Slip.Input.GetKey("F3") or Slip.Input.GetKey("G")) and self.gizmoCooldown <= 0.0 then
        self.gizmoCooldown = 0.4
        if self.vehicle then
            self.vehicle.showDebugGizmos = not self.vehicle.showDebugGizmos
        end
    end

    -- Recuperación / Reinicio seguro de vuelco (tecla R)
    if not self.resetCooldown then self.resetCooldown = 0.0 end
    self.resetCooldown = self.resetCooldown - dt

    local resetRequested = false
    if Slip.Input and Slip.Input.GetKeyDown then
        resetRequested = Slip.Input.GetKeyDown("R")
    elseif Slip.Input and Slip.Input.GetKey then
        resetRequested = Slip.Input.GetKey("R") and (self.resetCooldown <= 0.0)
    end

    if resetRequested then
        self.resetCooldown = 0.6 -- Cooldown de 0.6s para no ejecutar múltiples veces al mantener presionada la tecla
        if self.rigidBody then
            self.rigidBody:SetLinearVelocity(Vec3(0, 0, 0))
            self.rigidBody:SetAngularVelocity(Vec3(0, 0, 0))

            local posX = self.transform.position.x
            local posZ = self.transform.position.z
            local currentY = self.transform.position.y

            -- Si cayó al vacío fuera del plano de la pista, regresar al centro de la pista
            if currentY < -2.0 or math.abs(posX) > 130.0 or math.abs(posZ) > 130.0 then
                posX = 0.0
                posZ = 0.0
            end

            -- Colocar el vehículo a la altura exacta de reposo (0.85m sobre el asfalto)
            -- de modo que las 4 ruedas inicien ya apoyadas sobre la pista sin impacto de caída
            self.rigidBody:SetPosition(Vec3(posX, 0.85, posZ))
            self.rigidBody:SetRotation(Vec3(0, self.transform.rotation.y, 0))

            if self.vehicle and self.vehicle.ResetSuspension then
                self.vehicle:ResetSuspension()
            end
        end
    end

    -- Carga aerodinámica (Downforce) a alta velocidad para estabilidad en curvas rápidas
    if absSpeed > 60.0 and self.rigidBody then
        local downforceRatio = (absSpeed / 100.0)
        local downforce = downforceRatio * downforceRatio * 1600.0 -- Proporcional al cuadrado de la velocidad
        self.rigidBody:AddForce(Vec3(0, -downforce, 0))
    end

    -- 2. GESTIÓN DEL DERRAPE (Modern NFS Drift Physics: Freno de Mano en Space)
    -- Detección de Freno de Mano: Toque Rápido (Tap) vs Mantener Presionado (Hold)
    if not self.handbrakeHoldTimer then self.handbrakeHoldTimer = 0.0 end
    local isHandbrakeJustPressed = false

    if handbrake then
        if not self.wasHandbrakePressed then
            isHandbrakeJustPressed = true
        end
        self.wasHandbrakePressed = true
        self.handbrakeHoldTimer = self.handbrakeHoldTimer + dt
    else
        self.wasHandbrakePressed = false
        self.handbrakeHoldTimer = 0.0
    end

    if not self.driftTimer then self.driftTimer = 0.0 end

    -- Iniciar Derrape: Un toque rápido (TAP) del freno de mano a velocidad rompe tracción trasera
    if isHandbrakeJustPressed and absSpeed > 16.0 then
        self.isDrifting = true
        self.driftTimer = 1.4 -- Duración suficiente para trazar una curva amplia

        -- Determinar dirección de la curva del derrape (+1 = Izquierda, -1 = Derecha)
        local dir = 1.0
        if steerInput ~= 0.0 then
            dir = (steerInput > 0.0) and 1.0 or -1.0
        elseif math.abs(self.currentSteering) > 0.03 then
            dir = (self.currentSteering > 0.0) and 1.0 or -1.0
        end
        self.driftDirection = dir
    end

    -- Cálculo del ángulo de deslizamiento lateral (Slip Angle) usando trigonometría continua 2D
    local slipAngle = 0.0
    if self.rigidBody and absSpeed > 8.0 then
        local vel = self.rigidBody:GetLinearVelocity()
        local vLen = math.sqrt(vel.x * vel.x + vel.z * vel.z)
        if vLen > 1.2 then
            local rad = math.rad(self.transform.rotation.y)
            local fwdX = math.sin(rad)
            local fwdZ = math.cos(rad)
            local velX = vel.x / vLen
            local velZ = vel.z / vLen
            local dot = math.max(-1.0, math.min(1.0, fwdX * velX + fwdZ * velZ))
            local cross = fwdX * velZ - fwdZ * velX
            slipAngle = math.deg(math.atan(cross, dot))
        end
    end

    if self.isDrifting then
        self.driftTimer = self.driftTimer - dt

        -- Terminar derrape si cae de velocidad o si el conductor frena con pedal 'S'
        if absSpeed < 14.0 or (throttle < -0.3 and not handbrake) then
            self.isDrifting = false
        end

        -- Si el conductor contravolantea hacia afuera de la curva para salir:
        local isCounterSteeringOut = (steerInput * self.driftDirection < -0.25)
        if isCounterSteeringOut and self.driftTimer < 0.6 then
            self.isDrifting = false
        end
    end

    -- Dinámica de derrape estilo Arcade / Need for Speed:
    -- El coche se coloca en un ángulo sostenido (~24°) y se estabiliza para deslizarse de lado sin trompear ni invertir el giro
    if self.isDrifting and self.rigidBody and absSpeed > 15.0 then
        local targetSlip = self.driftDirection * 24.0 -- Ángulo ideal hacia el interior de la curva
        local angleDiff = targetSlip - slipAngle
        local angVel = self.rigidBody:GetAngularVelocity()

        -- Amortiguador de guiñada: estabiliza el chasis exactamente en el ángulo de derrape
        -- para que el coche se vea quieto en ese ángulo mientras se desplaza lateralmente
        local desiredYaw = math.max(-1.6, math.min(1.6, angleDiff * 0.09))
        -- El volante del jugador modula la curvatura de la trayectoria:
        desiredYaw = desiredYaw + (steerInput * 0.75)

        local currentYaw = angVel.y
        local yawCorrection = (desiredYaw - currentYaw) * math.min(1.0, dt * 9.0)
        self.rigidBody:SetAngularVelocity(Vec3(angVel.x, currentYaw + yawCorrection, angVel.z))

        -- Agarre de neumáticos: ruedas delanteras con agarre total para dirigir la curva (5.5)
        -- y ruedas traseras con menor fricción (3.1) para mantener el deslizamiento lateral continuo
        local rearGrip = 3.1
        if math.abs(slipAngle) > 28.0 then
            rearGrip = 4.8 -- Si se pasa de ángulo, aumentar agarre trasero para evitar trompo
        end

        if self.vehicle.SetTireFriction then
            self.vehicle:SetTireFriction(5.5, rearGrip)
        else
            self.vehicle.frictionSlip = rearGrip
        end

        -- Recarga del depósito de Nitro
        local driftRecharge = (absSpeed / 100.0) * self.nitroRechargeRate * dt
        self.nitroFuel = math.min(1.0, self.nitroFuel + driftRecharge)
    else
        -- Agarre firme normal en todas las ruedas
        if self.vehicle.SetTireFriction then
            self.vehicle:SetTireFriction(self.baseTireGrip, self.baseTireGrip)
        else
            self.vehicle.frictionSlip = self.baseTireGrip
        end
        if absSpeed > 90.0 and throttle > 0.5 then
            self.nitroFuel = math.min(1.0, self.nitroFuel + (0.015 * dt))
        end
    end

    -- 3. DIRECCIÓN SENSIBLE A LA VELOCIDAD
    -- A baja velocidad (0-40 km/h): giro amplio para maniobras y curvas cerradas.
    -- A alta velocidad (100-160+ km/h): se reduce progresivamente a ~6-9° para evitar giros bruscos o trompos en recta.
    local speedSteerFactor = 1.0 / (1.0 + (absSpeed / 75.0) * 1.8)
    speedSteerFactor = math.max(0.20, speedSteerFactor)

    if self.isDrifting then
        -- Durante el derrape, habilitar mayor ángulo de volante (~15°) para controlar la trazada de la curva
        speedSteerFactor = math.max(speedSteerFactor, 0.45)
    end

    local targetSteerAngle = steerInput * (self.maxSteeringAngle * speedSteerFactor)
    self.currentSteering = self.currentSteering + (targetSteerAngle - self.currentSteering) * math.min(1.0, dt * self.steeringSpeed)
    self.vehicle:SetSteering(self.currentSteering)

    -- 4. SISTEMA DE NITRO (Boost, Aceleración extra y Efectos)
    self.nitroActive = (nitroKey and self.nitroFuel > 0.05 and throttle > 0.2 and absSpeed > 10.0)
    local boostMultiplier = 1.0

    if self.nitroActive then
        self.nitroFuel = math.max(0.0, self.nitroFuel - (self.nitroConsumeRate * dt))
        boostMultiplier = self.nitroMultiplier

        -- Empuje físico hacia adelante en el chasis
        if self.rigidBody then
            local rad = math.rad(self.transform.rotation.y)
            local fwd = Vec3(math.sin(rad), 0.0, math.cos(rad))
            local currentVel = self.rigidBody:GetLinearVelocity()
            self.rigidBody:SetLinearVelocity(currentVel + (fwd * (self.nitroImpulse * dt * 0.01)))
        end

        -- Sacudida sutil de cámara estilo acción rápida
        if Slip.Camera and Slip.Camera.Shake then
            Slip.Camera.Shake(0.06, 0.15, 32.0)
        end
    end

    -- 5. TRANSMISIÓN DE 6 VELOCIDADES Y CÁLCULO DE RPM
    self:UpdateGearAndRPM(dt, absSpeed, throttle)

    -- 6. APLICACIÓN DE FUERZA DE MOTOR Y FRENADO (Power Oversteer en derrapes)
    if self.shiftTimer > 0.0 then
        -- Desconexión momentánea de par durante cambio de marcha
        self.shiftTimer = self.shiftTimer - dt
        self.vehicle:SetEngineForce(0.0)
        self.vehicle:SetBrake(0.0)
        self.vehicle:SetHandBrake(0.0)
    elseif handbrake then
        if self.handbrakeHoldTimer > 0.22 then
            -- MANTENER PRESIONADO EL FRENO DE MANO: Freno de emergencia real y contundente para detener el coche
            self.vehicle:SetHandBrake(95.0)
            self.vehicle:SetBrake(self.maxBrakeForce * 0.9)
            self.vehicle:SetEngineForce(0.0)
        else
            -- TOQUE RÁPIDO (TAP) DEL FRENO DE MANO: Romper tracción trasera para iniciar drift sin frenar en seco
            self.vehicle:SetHandBrake(12.0)
            self.vehicle:SetBrake(0.0)
            if throttle > 0.05 then
                local force = throttle * self.maxEngineForce * 1.15 * boostMultiplier
                self.vehicle:SetEngineForce(force)
            else
                self.vehicle:SetEngineForce(0.0)
            end
        end
    elseif throttle > 0.05 then
        self.vehicle:SetHandBrake(0.0)
        self.vehicle:SetBrake(0.0)

        -- Corte de inyección en redline
        local powerCut = 1.0
        if self.currentRPM >= self.maxRPM then
            powerCut = 0.15
        end

        local force = throttle * self.maxEngineForce * boostMultiplier * powerCut
        if self.isDrifting then
            force = force * 1.25 -- Potencia extra para sostener el derrape
        end
        self.vehicle:SetEngineForce(force)

    elseif throttle < -0.05 then
        self.vehicle:SetHandBrake(0.0)
        if currentSpeedKmH > 3.0 then
            -- Frenado normal progresivo y seguro (sin bloquear ruedas delanteras)
            self.vehicle:SetEngineForce(0.0)
            self.vehicle:SetBrake(self.maxBrakeForce)
        else
            -- Marcha atrás (Reversa)
            self.vehicle:SetBrake(0.0)
            self.vehicle:SetEngineForce(throttle * self.maxEngineForce * 0.55)
        end
    else
        -- Conductor suelta pedales (inercia)
        self.vehicle:SetEngineForce(0.0)
        self.vehicle:SetHandBrake(0.0)
        if absSpeed > 1.0 then
            self.vehicle:SetBrake(self.maxBrakeForce * 0.08)
        else
            self.vehicle:SetBrake(self.maxBrakeForce * 0.4)
        end
    end

    -- 7. ACTUALIZACIÓN DE HUD EN TIEMPO REAL
    self:UpdateHUD(absSpeed)
end

-- ----------------------------------------------------------------------------
-- Simulación de Caja de Cambios y Curva de RPM
-- ----------------------------------------------------------------------------
function Script:UpdateGearAndRPM(dt, speedKmH, throttle)
    -- Si estamos en marcha atrás o intentando retroceder
    if self.currentGear == -1 or (speedKmH < 3.0 and throttle < -0.1) then
        self.currentGear = -1 -- Reversa
        if speedKmH < 3.0 and throttle >= 0.05 then
            self.currentGear = 1
        else
            local ratio = math.min(1.0, speedKmH / 35.0)
            self.targetRPM = self.idleRPM + ratio * (self.redlineRPM - self.idleRPM)
            local rpmSpeed = (self.targetRPM > self.currentRPM) and 12.0 or 8.0
            self.currentRPM = self.currentRPM + (self.targetRPM - self.currentRPM) * math.min(1.0, dt * rpmSpeed)
            self.currentRPM = math.max(self.idleRPM, math.min(self.maxRPM, self.currentRPM))
            return
        end
    end

    if self.currentGear < 1 then
        self.currentGear = 1
    end

    -- Lógica de Subida de Marcha (Upshift)
    if self.currentGear < 6 then
        local nextGearMax = self.gearSpeeds[self.currentGear] or 50.0
        if speedKmH > (nextGearMax * 0.94) and throttle > 0.4 then
            self.currentGear = self.currentGear + 1
            self.shiftTimer = self.shiftDuration
        end
    end

    -- Lógica de Bajada de Marcha (Downshift)
    if self.currentGear > 1 then
        local prevGearMax = self.gearSpeeds[self.currentGear - 1] or 40.0
        if speedKmH < (prevGearMax * 0.70) then
            self.currentGear = self.currentGear - 1
            self.shiftTimer = self.shiftDuration * 0.5
        end
    end

    -- Cálculo de RPM en la marcha actual
    local gearMinSpeed = (self.currentGear > 1) and ((self.gearSpeeds[self.currentGear - 1] or 0.0) * 0.6) or 0.0
    local gearMaxSpeed = self.gearSpeeds[self.currentGear] or 280.0

    local speedInGear = math.max(0.0, speedKmH - gearMinSpeed)
    local gearRange = math.max(10.0, gearMaxSpeed - gearMinSpeed)
    local rpmFactor = math.min(1.0, speedInGear / gearRange)

    if math.abs(throttle) < 0.05 then
        self.targetRPM = self.idleRPM + (rpmFactor * (self.redlineRPM - self.idleRPM) * 0.6)
    else
        self.targetRPM = self.idleRPM + (rpmFactor * (self.redlineRPM - self.idleRPM))
    end

    -- Acelerón en parado o a muy baja velocidad
    if speedKmH < 6.0 and throttle > 0.2 then
        self.targetRPM = math.max(self.targetRPM, 2800.0 * throttle)
    end

    -- Suavizado analógico de la aguja del tacómetro
    local rpmSpeed = (self.targetRPM > self.currentRPM) and 12.0 or 8.0
    self.currentRPM = self.currentRPM + (self.targetRPM - self.currentRPM) * math.min(1.0, dt * rpmSpeed)
    self.currentRPM = math.max(self.idleRPM, math.min(self.maxRPM, self.currentRPM))
end

-- ----------------------------------------------------------------------------
-- Actualización del HUD (Velocímetro, Tacómetro, Marcha y Barra Nitro)
-- ----------------------------------------------------------------------------
function Script:UpdateHUD(speedKmH)
    if not self.speedTextEnt or not self.gearTextEnt or not self.rpmTextEnt then
        self:FindHUDEntities()
    end

    -- 1. Velocímetro
    if self.speedTextEnt then
        local uiTxt = self.speedTextEnt:GetUIText()
        if uiTxt then
            uiTxt:SetText(string.format("%d", math.floor(speedKmH)) .. " KM/H")
        end
    end

    -- 2. Marcha Actual
    if self.gearTextEnt then
        local uiTxt = self.gearTextEnt:GetUIText()
        if uiTxt then
            local gearStr = tostring(self.currentGear)
            if self.currentGear == -1 then
                gearStr = "R"
            elseif self.currentGear == 0 or speedKmH < 0.5 then
                gearStr = (self.currentGear == -1) and "R" or "1"
            end
            uiTxt:SetText(gearStr)
        end
    end

    -- 3. Tacómetro RPM
    if self.rpmTextEnt then
        local uiTxt = self.rpmTextEnt:GetUIText()
        if uiTxt then
            uiTxt:SetText(string.format("%d RPM", math.floor(self.currentRPM)))
            -- Alerta visual de Redline
            if self.currentRPM >= 6800.0 then
                uiTxt.color = Vec4(1.0, 0.15, 0.15, 1.0) -- Rojo vivo
            elseif self.currentRPM >= 5800.0 then
                uiTxt.color = Vec4(1.0, 0.85, 0.1, 1.0)  -- Amarillo / Ámbar
            else
                uiTxt.color = Vec4(1.0, 1.0, 1.0, 1.0)  -- Blanco
            end
        end
    end

    -- 4. Barra de Nitro
    if self.nitroBarEnt then
        local pb = self.nitroBarEnt:GetUIProgressBar()
        if pb then
            pb.value = self.nitroFuel * 100.0
            if pb.SetValue then
                pb:SetValue(self.nitroFuel * 100.0)
            end
            if self.nitroActive then
                pb.fillColor = Vec4(0.2, 0.9, 1.0, 1.0) -- Cian eléctrico en boost
            else
                pb.fillColor = Vec4(0.1, 0.5, 0.95, 1.0) -- Azul intenso en reposo
            end
        end
    end
end

return Script
