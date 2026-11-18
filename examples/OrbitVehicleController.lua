-- ============================================================================
-- OrbitVehicleController.lua
-- Control de vehículo con Cámara Orbital 360° fluida estilo GTA
-- ============================================================================

local Script = {}

-- ----------------------------------------------------------------------------
-- Propiedades configurables desde el Inspector de SlipEngine
-- ----------------------------------------------------------------------------
-- Cámara Orbital
Script.cameraName = "Camara"            -- Nombre de la cámara en la escena
Script.cameraDistance = 6.0             -- Distancia detrás del vehículo
Script.cameraHeight = 1.3               -- Altura del pivote sobre el centro del vehículo
Script.mouseSensitivity = 0.15          -- Sensibilidad de rotación del mouse
Script.minPitch = -12.0                 -- Límite al mirar hacia arriba desde abajo (grados)
Script.maxPitch = 55.0                  -- Límite al mirar hacia abajo desde arriba (grados)
Script.invertY = true                   -- Eje vertical invertido a petición del usuario

-- Vehículo y Conducción
Script.maxSpeedKmH = 75.0               -- Velocidad máxima en km/h
Script.maxEngineForce = 2200.0          -- Fuerza de aceleración del motor
Script.maxBrakeForce = 120.0            -- Fuerza de frenado normal
Script.handBrakeForce = 220.0           -- Freno de mano suave para derrapes (Barra Espaciadora)
Script.maxSteeringAngle = 0.50          -- Ángulo máximo de giro de ruedas (~28 grados)
Script.steeringResponse = 6.0           -- Rapidez de respuesta del volante
Script.autoCenterCamera = true          -- Autocentrado suave de la cámara al avanzar

-- Calibración física de suspensión (Elimina el jaloneo por oscilación y evita que se hunda)
Script.suspensionStiffness = 32.0       -- Rigidez calculada para sostener 1200kg sin oscilar
Script.suspensionDamping = 4.2          -- Amortiguación crítica para matar rebotes y jaloneos
Script.suspensionCompression = 3.2      -- Amortiguación de compresión
Script.maxSuspensionForce = 8500.0      -- Fuerza máxima de suspensión
Script.maxSuspensionTravelCm = 70.0     -- Recorrido controlado (70 cm, evita efecto acordeón)
Script.tireGrip = 5.5                   -- Fricción lateral equilibrada para giros y derrapes

function Script:GetCarVehicle()
    if self.entity then
        local v = self.entity:GetVehicle()
        if v then return v end
        return self.entity:GetComponent("Vehicle")
    end
    return nil
end

function Script:OnStart()
    self.camYaw = 0.0
    self.camPitch = 15.0                -- Inclinación inicial centrada en el auto
    self.currentSteering = 0.0
    self.camEntity = nil

    -- 1. Obtener componente de vehículo
    self.vehicle = self:GetCarVehicle()

    -- 2. Configurar suspensión suave y calibrada en Bullet
    if self.vehicle then
        self.vehicle.suspensionStiffness = self.suspensionStiffness
        self.vehicle.suspensionDamping = self.suspensionDamping
        self.vehicle.suspensionCompression = self.suspensionCompression
        self.vehicle.maxSuspensionForce = self.maxSuspensionForce
        self.vehicle.maxSuspensionTravelCm = self.maxSuspensionTravelCm
        self.vehicle.frictionSlip = self.tireGrip
        self.vehicle.rollInfluence = 0.08 -- Centro de balance bajo para evitar vuelcos fáciles
    end

    -- 3. Configurar fricción y amortiguación de la carrocería
    local rb = (self.entity and self.entity:GetRigidBody()) or self:GetRigidBody()
    if rb then
        rb:SetFriction(0.3)
        rb.angularDrag = 0.3
    end

    -- 4. Inicializar ángulo de la cámara alineado detrás del coche
    local t = (self.entity and self.entity:GetTransform()) or self:GetTransform()
    if t then
        self.camYaw = t.rotation.y or 0.0
    end

    -- 5. Localizar la cámara en la escena
    self:FindCamera()

    -- 6. Bloquear y ocultar el cursor del mouse
    Slip.Input.SetCursorLocked(true)
    Slip.Input.SetCursorVisible(false)
end

function Script:FindCamera()
    local names = { self.cameraName, "Camara", "Camera", "MainCamera", "Cámara" }
    for _, name in ipairs(names) do
        local ent = Slip.Scene.FindEntity(name)
        if ent and ent:GetComponent("Camera") then
            self.camEntity = ent
            return
        end
    end

    for _, ent in ipairs(Slip.Scene.GetEntities()) do
        if ent and ent:GetComponent("Camera") then
            self.camEntity = ent
            return
        end
    end
end

function Script:OnUpdate(dt)
    if not self.vehicle then
        self.vehicle = self:GetCarVehicle()
        if not self.vehicle then return end
    end

    local carTransform = (self.entity and self.entity:GetTransform()) or self:GetTransform()
    if not carTransform then return end

    -- Mantener la suspensión calibrada
    self.vehicle.suspensionStiffness = self.suspensionStiffness
    self.vehicle.suspensionDamping = self.suspensionDamping
    self.vehicle.suspensionCompression = self.suspensionCompression
    self.vehicle.maxSuspensionForce = self.maxSuspensionForce
    self.vehicle.maxSuspensionTravelCm = self.maxSuspensionTravelCm

    -- ========================================================================
    -- 1. ROTACIÓN ORBITAL CON EL MOUSE (Eje Y invertido según lo solicitado)
    -- ========================================================================
    local mouseDelta = Slip.Input.GetMouseDelta()

    -- Movimiento horizontal (Yaw): orbita 360° libremente alrededor del vehículo
    self.camYaw = self.camYaw - (mouseDelta.x * self.mouseSensitivity)
    if self.camYaw >= 360.0 then self.camYaw = self.camYaw - 360.0 end
    if self.camYaw < 0.0 then self.camYaw = self.camYaw + 360.0 end

    -- Movimiento vertical (Pitch invertido):
    local pitchSign = self.invertY and 1.0 or -1.0
    self.camPitch = self.camPitch + (mouseDelta.y * self.mouseSensitivity * pitchSign)
    self.camPitch = math.max(self.minPitch, math.min(self.maxPitch, self.camPitch))

    -- ========================================================================
    -- 2. ENTRADA DEL CONDUCTOR (Prioridad Directa de Teclado W / S / A / D)
    -- ========================================================================
    local throttle = 0.0
    if Slip.Input.GetKey(Slip.Key.W) or Slip.Input.GetKey("W") or Slip.Input.GetKey(Slip.Key.Up) then
        throttle = 1.0
    elseif Slip.Input.GetKey(Slip.Key.S) or Slip.Input.GetKey("S") or Slip.Input.GetKey(Slip.Key.Down) then
        throttle = -1.0
    elseif Slip.Input and Slip.Input.GetAxis then
        local axisV = Slip.Input.GetAxis("Vertical")
        if math.abs(axisV) > 0.15 then
            throttle = axisV
        end
    end

    -- Control manual con A / D (A = Izquierda, D = Derecha)
    local steerManual = 0.0
    if Slip.Input.GetKey(Slip.Key.A) or Slip.Input.GetKey("A") or Slip.Input.GetKey(Slip.Key.Left) then
        steerManual = 1.0
    elseif Slip.Input.GetKey(Slip.Key.D) or Slip.Input.GetKey("D") or Slip.Input.GetKey(Slip.Key.Right) then
        steerManual = -1.0
    elseif Slip.Input and Slip.Input.GetAxis then
        local axisH = -Slip.Input.GetAxis("Horizontal")
        if math.abs(axisH) > 0.15 then
            steerManual = axisH
        end
    end

    local handbrake = Slip.Input.GetKey(Slip.Key.Space) or Slip.Input.GetKey("Space")

    -- Tecla R: Volver a poner el coche sobre sus 4 ruedas si vuelca
    if Slip.Input.GetKey(Slip.Key.R) or Slip.Input.GetKey("R") then
        local rb = (self.entity and self.entity:GetRigidBody()) or self:GetRigidBody()
        if rb then
            rb:SetLinearVelocity(Vec3(0, 0, 0))
            rb:SetAngularVelocity(Vec3(0, 0, 0))
            rb:SetPosition(Vec3(carTransform.position.x, carTransform.position.y + 1.5, carTransform.position.z))
            rb:SetRotation(Vec3(0, carTransform.rotation.y, 0))
        end
    end

    -- Protección anti-caída al vacío si el vehículo traspasa el terreno
    if carTransform.position.y < -30.0 then
        local rb = (self.entity and self.entity:GetRigidBody()) or self:GetRigidBody()
        if rb then
            rb:SetLinearVelocity(Vec3(0, 0, 0))
            rb:SetAngularVelocity(Vec3(0, 0, 0))
            rb:SetPosition(Vec3(carTransform.position.x, 5.0, carTransform.position.z))
            rb:SetRotation(Vec3(0, carTransform.rotation.y, 0))
        end
    end

    -- ========================================================================
    -- 3. DIRECCIÓN INTELIGENTE
    -- ========================================================================
    local targetSteering = 0.0
    local carYaw = carTransform.rotation.y

    if math.abs(steerManual) > 0.05 then
        targetSteering = steerManual * self.maxSteeringAngle
        -- Alinear la cámara suavemente detrás del auto al maniobrar manualmente
        local diff = carYaw - self.camYaw
        while diff > 180.0 do diff = diff - 360.0 end
        while diff < -180.0 do diff = diff + 360.0 end
        self.camYaw = self.camYaw + diff * math.min(1.0, dt * 2.5)

    elseif throttle > 0.05 then
        local diffAngle = self.camYaw - carYaw
        while diffAngle > 180.0 do diffAngle = diffAngle - 360.0 end
        while diffAngle < -180.0 do diffAngle = diffAngle + 360.0 end

        if math.abs(diffAngle) < 2.0 then
            targetSteering = 0.0
        else
            local angleRad = math.rad(diffAngle)
            targetSteering = math.max(-self.maxSteeringAngle, math.min(self.maxSteeringAngle, angleRad * 1.5))
        end

        if self.autoCenterCamera and math.abs(mouseDelta.x) < 0.05 then
            local trackDiff = carYaw - self.camYaw
            while trackDiff > 180.0 do trackDiff = trackDiff - 360.0 end
            while trackDiff < -180.0 do trackDiff = trackDiff + 360.0 end
            self.camYaw = self.camYaw + trackDiff * math.min(1.0, dt * 1.8)
        end

    else
        targetSteering = 0.0
    end

    self.currentSteering = self.currentSteering + (targetSteering - self.currentSteering) * math.min(1.0, dt * self.steeringResponse)
    self.vehicle:SetSteering(self.currentSteering)

    -- ========================================================================
    -- 4. POTENCIA FLUIDA Y FRENO DE MANO PARA DERRAPES REALISTAS
    -- ========================================================================
    local currentSpeed = self.vehicle:GetSpeed() or 0.0
    local speedKmH = math.abs(currentSpeed)

    if handbrake then
        -- Freno de mano: derrape suave en ruedas traseras sin frenazo seco tipo ladrillo
        self.vehicle:SetEngineForce(0.0)
        self.vehicle:SetBrake(0.0)
        self.vehicle:SetHandBrake(self.handBrakeForce)

    elseif throttle > 0.05 then
        -- Aceleración suave hacia adelante
        self.vehicle:SetHandBrake(0.0)
        self.vehicle:SetBrake(0.0)

        local forceFactor = 1.0
        if speedKmH >= self.maxSpeedKmH then
            forceFactor = 0.0
        elseif speedKmH > (self.maxSpeedKmH * 0.8) then
            forceFactor = (self.maxSpeedKmH - speedKmH) / (self.maxSpeedKmH * 0.2)
        end

        self.vehicle:SetEngineForce(throttle * self.maxEngineForce * forceFactor)

    elseif throttle < -0.05 then
        -- Freno de servicio o marcha atrás
        self.vehicle:SetHandBrake(0.0)
        if currentSpeed > 3.0 then
            self.vehicle:SetEngineForce(0.0)
            self.vehicle:SetBrake(self.maxBrakeForce * 1.5)
        else
            self.vehicle:SetBrake(0.0)
            self.vehicle:SetEngineForce(throttle * self.maxEngineForce * 0.6)
        end

    else
        -- Conductor soltó los pedales: inercia suave
        self.vehicle:SetEngineForce(0.0)
        self.vehicle:SetHandBrake(0.0)
        if speedKmH > 1.0 then
            self.vehicle:SetBrake(self.maxBrakeForce * 0.05)
        else
            self.vehicle:SetBrake(self.maxBrakeForce * 0.3)
        end
    end

    -- Actualizar posición y orientación orbital de la cámara
    self:UpdateCameraPosition(carTransform)
end

-- ============================================================================
-- Posicionamiento de la Cámara Centrado en el Auto
-- ============================================================================
function Script:UpdateCameraPosition(carTransform)
    if not self.camEntity then
        self:FindCamera()
        if not self.camEntity then return end
    end

    local camTransform = self.camEntity:GetTransform()
    if not camTransform then return end

    local carPos = carTransform.position
    local pivot = Vec3(carPos.x, carPos.y + self.cameraHeight, carPos.z)

    local desiredDist = self.cameraDistance
    local radYaw = math.rad(self.camYaw)
    local radPitch = math.rad(self.camPitch)
    local cosPitch = math.cos(radPitch)
    local sinPitch = math.sin(radPitch)

    -- Vector orbital hacia atrás y arriba del vehículo
    local backDir = Vec3(
        -math.sin(radYaw) * cosPitch,
        sinPitch,
        -math.cos(radYaw) * cosPitch
    )

    -- Raycast anti-clipping con el terreno u obstáculos
    if Slip.Physics and Slip.Physics.Raycast then
        local rayFrom = pivot
        local rayTo = pivot + (backDir * self.cameraDistance)
        local hit = Slip.Physics.Raycast(rayFrom, rayTo, self.entity)
        if hit and hit.hasHit and hit.distance then
            desiredDist = math.max(1.5, hit.distance - 0.3)
        end
    end

    local targetPos = pivot + (backDir * desiredDist)
    if targetPos.y < (carPos.y + 0.3) then
        targetPos.y = carPos.y + 0.3
    end
    camTransform.position = targetPos

    -- Rotación: Horizonte nivelado sin roll en Z
    camTransform.rotation = Vec3(-self.camPitch, self.camYaw + 180.0, 0.0)
end

return Script
