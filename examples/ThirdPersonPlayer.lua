-- ============================================================================
-- PlayerController.lua / ThirdPersonPlayer.lua
-- Controlador de Jugador en 3ra Persona con Anti-Clipping de Cámara (SpringArm)
-- ============================================================================

local Script = {}

-- Propiedades configurables desde el Inspector de SlipEngine
Script.moveSpeed = 6.0
Script.jumpSpeed = 7.0
Script.mouseSensitivity = 0.15
Script.minPitch = -60.0             -- Límite al mirar hacia abajo (grados)
Script.maxPitch = 60.0              -- Límite al mirar hacia arriba (grados)
Script.cameraName = "Camara"        -- Nombre de la entidad cámara (o hijo)
Script.cameraDistance = 3.5         -- Distancia máxima detrás del jugador
Script.cameraHeight = 1.8           -- Altura del pivote de la cámara (ojos/cabeza)
Script.minCameraDistance = 0.5      -- Distancia mínima ante obstáculos
Script.cameraCollisionOffset = 0.25 -- Margen para no cortar polígonos
Script.cameraZoomSpeed = 10.0       -- Velocidad de recuperación de distancia
Script.lockCursorOnStart = true     -- Bloquear cursor al iniciar el juego
Script.invertY = false              -- Invertir eje vertical del ratón

function Script:OnStart()
    self.grounded = false
    self.isJumping = false
    self.pitch = 0.0
    self.currentDistance = self.cameraDistance
    self.camEntity = nil
    self.camIsChild = false

    -- Inicializar ángulo Yaw desde la rotación actual del Transform
    local transform = self:GetTransform()
    if transform then
        self.yaw = transform.rotation.y or 0.0
    else
        self.yaw = 0.0
    end

    -- Configurar física del RigidBody (sin fricción en paredes para caída libre fluida)
    local body = self:GetRigidBody()
    if body and body.SetFriction then
        body:SetFriction(0.0)
    end

    -- Localizar la cámara
    self:FindCamera()

    -- Bloquear y ocultar el cursor del mouse
    if self.lockCursorOnStart then
        Slip.Input.SetCursorLocked(true)
        Slip.Input.SetCursorVisible(false)
    end
end

-- Busca la cámara en la jerarquía o en la escena
function Script:FindCamera()
    -- 1. Buscar entre los hijos de esta entidad
    if self.entity then
        local namesToTry = { self.cameraName, "Camara", "Camera", "MainCamera", "Main Camera", "PlayerCamera" }
        for _, name in ipairs(namesToTry) do
            if name and name ~= "" then
                local child = self.entity:FindChild(name, true)
                if child then
                    self.camEntity = child
                    self.camIsChild = true
                    return
                end
            end
        end

        for _, child in ipairs(self.entity:GetChildren()) do
            if child and child:GetComponent("Camera") then
                self.camEntity = child
                self.camIsChild = true
                return
            end
        end
    end

    -- 2. Buscar en toda la escena (si no es hija)
    local sceneNames = { self.cameraName, "Camara", "Camera", "MainCamera", "Main Camera" }
    for _, name in ipairs(sceneNames) do
        if name and name ~= "" then
            local ent = Slip.Scene.FindEntity(name)
            if ent then
                self.camEntity = ent
                self.camIsChild = (self.entity and ent:IsChildOf(self.entity)) or false
                return
            end
        end
    end

    -- 3. Buscar cualquier entidad en la escena con CameraComponent
    for _, ent in ipairs(Slip.Scene.GetEntities()) do
        if ent and ent:GetComponent("Camera") then
            self.camEntity = ent
            self.camIsChild = (self.entity and ent:IsChildOf(self.entity)) or false
            return
        end
    end
end

function Script:OnUpdate(dt)
    -- 1. Control del Cursor (Re-bloquear al hacer clic si el usuario lo liberó)
    if not Slip.Input.IsCursorLocked() and (Slip.Input.GetMouseButton(0) or Slip.Input.GetMouseButton(1)) then
        Slip.Input.SetCursorLocked(true)
        Slip.Input.SetCursorVisible(false)
    end

    -- 2. Rotación continua con el Mouse (sin inversión a 90°)
    local mouseDelta = Slip.Input.GetMouseDelta()

    -- Eje Y del jugador: rotación horizontal continua
    self.yaw = self.yaw - (mouseDelta.x * self.mouseSensitivity)
    if self.yaw >= 360.0 then self.yaw = self.yaw - 360.0 end
    if self.yaw < 0.0 then self.yaw = self.yaw + 360.0 end

    -- Eje X de la cámara: rotación vertical (Pitch)
    local deltaY = mouseDelta.y * self.mouseSensitivity
    if self.invertY then
        self.pitch = self.pitch + deltaY
    else
        self.pitch = self.pitch - deltaY
    end
    if self.pitch > self.maxPitch then self.pitch = self.maxPitch end
    if self.pitch < self.minPitch then self.pitch = self.minPitch end

    -- Aplicar rotación Yaw al Transform y al RigidBody
    local transform = self:GetTransform()
    if transform then
        transform.rotation = Vec3(0, self.yaw, 0)
    end

    local body = self:GetRigidBody()
    if body and body.SetRotation then
        body:SetRotation(Vec3(0, self.yaw, 0))
    end

    -- 3. Vectores de dirección en el plano XZ (OpenGL: adelante es -Z, derecha es +X)
    local rad = math.rad(self.yaw)
    local forward = Vec3(-math.sin(rad), 0, -math.cos(rad))
    local right   = Vec3( math.cos(rad), 0, -math.sin(rad))

    -- 4. Entrada de movimiento (WASD / Stick)
    local horizontal = Slip.Input.GetAxis("Horizontal")
    local vertical   = Slip.Input.GetAxis("Vertical")

    local moveDir = (forward * vertical) + (right * horizontal)
    local moveMagnitude = moveDir:Length()

    if moveMagnitude > 0.001 then
        moveDir = moveDir:Normalize()

        -- Anti-pegado a paredes y cortinas: si nos movemos contra una colisión, proyectar tangencialmente
        if transform and Slip.Physics and Slip.Physics.Raycast then
            local pos = transform.position
            local hitWall = nil
            local heights = { 1.3, 0.8, 0.3 }

            for _, h in ipairs(heights) do
                local wallFrom = Vec3(pos.x, pos.y + h, pos.z)
                local wallTo = Vec3(pos.x + moveDir.x * 0.85, pos.y + h, pos.z + moveDir.z * 0.85)
                local hit = Slip.Physics.Raycast(wallFrom, wallTo, self.entity)
                if hit and hit.hasHit and hit.normal and hit.normal.y < 0.707 then
                    hitWall = hit
                    break
                end
            end

            -- Si detectamos una pared u obstáculo en la dirección de avance
            if hitWall and hitWall.normal then
                -- Extraer normal en el plano horizontal (XZ)
                local nx = hitWall.normal.x
                local nz = hitWall.normal.z
                local nLen = math.sqrt(nx * nx + nz * nz)
                if nLen > 0.001 then
                    nx = nx / nLen
                    nz = nz / nLen
                    local dot = (moveDir.x * nx) + (moveDir.z * nz)
                    if dot < 0 then
                        -- Deslizar tangencialmente a la pared (elimina la fuerza que empuja hacia adentro)
                        moveDir = Vec3(moveDir.x - nx * dot, 0, moveDir.z - nz * dot)
                        local newMag = moveDir:Length()
                        if newMag > 0.001 then
                            moveDir = moveDir:Normalize()
                        else
                            moveDir = Vec3(0, 0, 0)
                        end
                    end
                end
            end
        end
    else
        moveDir = Vec3(0, 0, 0)
    end

    -- 5. Aplicar velocidad con físicas o traslación directa
    self.grounded = self:CheckGrounded()

    if body then
        local velocity = body:GetLinearVelocity()
        local moveY = velocity.y

        -- Salto voluntario
        if self.grounded and Slip.Input.GetActionDown("Jump") then
            moveY = self.jumpSpeed
            self.isJumping = true
            self.grounded = false
        end

        -- Prevención estricta de escalado de paredes / cortinas:
        -- Si el jugador NO está saltando intencionalmente, bajo ninguna circunstancia
        -- se debe permitir moveY > 0. Si una colisión inclinada (como los pliegues de las
        -- cortinas de Sponza) genera un impulso ascendente por penetración, lo anulamos a 0.
        if not self.isJumping then
            if moveY > 0.0 then
                moveY = 0.0
            end
        else
            -- Si ya alcanzamos el ápice del salto o empezamos a descender, terminar el estado de salto
            if velocity.y <= 0.0 then
                self.isJumping = false
            end
        end

        body:SetLinearVelocity(Vec3(moveDir.x * self.moveSpeed, moveY, moveDir.z * self.moveSpeed))
    else
        if transform and moveMagnitude > 0.001 then
            transform:Translate(moveDir.x * self.moveSpeed * dt, 0, moveDir.z * self.moveSpeed * dt)
        end
    end

    -- 6. Actualizar cámara con Anti-Clipping (Raycast SpringArm)
    self:UpdateCamera(dt)

    -- 7. Soporte para Animator (si existe)
    local animator = self.entity and self.entity:GetAnimator()
    if animator then
        local speedParam = moveMagnitude * self.moveSpeed
        animator:SetFloat("Speed", speedParam)
        animator:SetBool("IsMoving", moveMagnitude > 0.1)
    end

    self.grounded = false
end

-- Actualiza la rotación y posición de la cámara evitando atravesar paredes
function Script:UpdateCamera(dt)
    if not self.camEntity then
        self:FindCamera()
    end
    if not self.camEntity then return end

    local camTransform = self.camEntity:GetTransform()
    if not camTransform then return end

    local playerTransform = self:GetTransform()
    local pPos = playerTransform and playerTransform.position or Vec3(0, 0, 0)

    -- Pivote en la cabeza/ojos del jugador en coordenadas de mundo
    local pivot = Vec3(pPos.x, pPos.y + self.cameraHeight, pPos.z)

    -- Vector de orientación 3D de la cámara (Yaw + Pitch)
    local yawRad = math.rad(self.yaw)
    local pitchRad = math.rad(self.pitch)
    local cosPitch = math.cos(pitchRad)
    local sinPitch = math.sin(pitchRad)

    -- Vector hacia donde mira la cámara
    local camForward = Vec3(-math.sin(yawRad) * cosPitch, sinPitch, -math.cos(yawRad) * cosPitch)

    -- Posición deseada sin colisión (detrás del pivote a la distancia máxima)
    local rayFrom = pivot
    local rayTo = Vec3(
        pivot.x - camForward.x * self.cameraDistance,
        pivot.y - camForward.y * self.cameraDistance,
        pivot.z - camForward.z * self.cameraDistance
    )

    -- -------------------------------------------------------------
    -- Anti-Clipping (Raycast del jugador hacia la cámara)
    -- -------------------------------------------------------------
    local targetDist = self.cameraDistance
    if Slip.Physics and Slip.Physics.Raycast then
        local hit = Slip.Physics.Raycast(rayFrom, rayTo, self.entity)
        if hit and hit.hasHit and hit.distance then
            targetDist = math.max(self.minCameraDistance, hit.distance - self.cameraCollisionOffset)
        end
    end

    -- Acercamiento inmediato ante colisión; alejamiento suave al despejarse
    if targetDist < self.currentDistance then
        self.currentDistance = targetDist
    else
        self.currentDistance = self.currentDistance + (targetDist - self.currentDistance) * math.min(1.0, dt * self.cameraZoomSpeed)
    end

    if self.camIsChild then
        -- Caso A: La cámara es hija del jugador
        camTransform.position = Vec3(0, self.cameraHeight - sinPitch * self.currentDistance, cosPitch * self.currentDistance)
        camTransform.rotation = Vec3(self.pitch, 0, 0)
    else
        -- Caso B: La cámara es una entidad independiente en la escena
        camTransform.position = Vec3(
            pivot.x - camForward.x * self.currentDistance,
            pivot.y - camForward.y * self.currentDistance,
            pivot.z - camForward.z * self.currentDistance
        )
        camTransform.rotation = Vec3(self.pitch, self.yaw, 0)
    end
end

-- Comprobación precisa de suelo mediante Raycast hacia abajo
function Script:CheckGrounded()
    local transform = self:GetTransform()
    if not transform then return false end
    local pos = transform.position

    -- Raycast desde la base del jugador hacia abajo
    local rayFrom = Vec3(pos.x, pos.y + 0.15, pos.z)
    local rayTo   = Vec3(pos.x, pos.y - 0.25, pos.z)

    if Slip.Physics and Slip.Physics.Raycast then
        local hit = Slip.Physics.Raycast(rayFrom, rayTo, self.entity)
        if hit and hit.hasHit then
            -- Solo cuenta como suelo si la superficie es plana o rampa (normal.y > 0.55), no una pared vertical
            if hit.normal and hit.normal.y > 0.55 then
                return true
            end
        end
    end
    return false
end

function Script:OnCollisionStay(other)
    -- El estado de suelo lo determina CheckGrounded() para evitar falsos positivos en paredes
end

return Script