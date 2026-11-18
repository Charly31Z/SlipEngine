-- ============================================================================
-- PlatformerController2D.lua
-- Controlador de Plataformas 2D con inercia, aceleración y salto estilo Super Mario Bros
-- Diseñado para SlipEngine (RigidBody2D + BoxCollider2D + SpriteRenderer)
-- ============================================================================

local Script = {}

-- ----------------------------------------------------------------------------
-- Propiedades ajustables en tiempo real desde el Inspector de SlipEngine
-- ----------------------------------------------------------------------------
Script.walkSpeed = 5.5          -- Velocidad normal de caminata
Script.runSpeed = 9.0           -- Velocidad máxima al correr (manteniendo Shift / Run)
Script.acceleration = 24.0      -- Rapidez para alcanzar la velocidad máxima (inercia)
Script.friction = 20.0          -- Rapidez de frenado al soltar controles (suelo)
Script.airAcceleration = 14.0   -- Control en el aire
Script.jumpForce = 12.5         -- Fuerza inicial del salto
Script.jumpCutMultiplier = 0.5  -- Factor al soltar salto antes de tiempo (salto corto)
Script.coyoteTimeMax = 0.12     -- Ventana de gracia para saltar tras dejar una plataforma (s)
Script.jumpBufferMax = 0.12     -- Ventana de memoria para registrar salto antes de caer (s)

function Script:OnStart()
    -- Componentes
    self.rb2d = self:GetRigidBody2D() or (self.entity and self.entity:GetRigidBody2D())
    self.sprite = self:GetSpriteRenderer() or (self.entity and self.entity:GetSpriteRenderer())
    self.anim = self:GetSpriteAnimator() or (self.entity and self.entity:GetSpriteAnimator())
    self.transform = self:GetTransform() or (self.entity and self.entity:GetTransform())

    if self.rb2d then
        -- Bloquear rotación en el eje Z para que el personaje se mantenga erguido
        self.rb2d.freezeRotation = true
    end

    -- Estado interno
    self.currentVelocityX = 0.0
    self.isGrounded = false
    self.coyoteTimer = 0.0
    self.jumpBufferTimer = 0.0
    self.isJumping = false
    self.facingRight = true
end

function Script:OnUpdate(dt)
    if not self.rb2d then
        self.rb2d = self:GetRigidBody2D() or (self.entity and self.entity:GetRigidBody2D())
        if not self.rb2d then return end
    end

    local vel = self.rb2d:GetLinearVelocity()
    local vx = vel.x
    local vy = vel.y

    -- ------------------------------------------------------------------------
    -- 1. Captura de Entrada (Input Horizontal y Acciones)
    -- ------------------------------------------------------------------------
    local moveInput = 0.0

    -- Eje horizontal configurable (SlipInputMap o Joystick analógico)
    if Slip.Input and Slip.Input.GetAxis then
        moveInput = Slip.Input.GetAxis("Horizontal")
    end

    -- Lectura directa de teclas (A / D / Flechas izquierda / derecha)
    if moveInput == 0.0 and Slip.Input and Slip.Input.GetKey then
        local aKey = (Slip.Key and Slip.Key.A) or "A"
        local dKey = (Slip.Key and Slip.Key.D) or "D"
        local leftKey = (Slip.Key and Slip.Key.Left) or "Left"
        local rightKey = (Slip.Key and Slip.Key.Right) or "Right"

        if Slip.Input.GetKey(aKey) or Slip.Input.GetKey(leftKey) then
            moveInput = -1.0
        elseif Slip.Input.GetKey(dKey) or Slip.Input.GetKey(rightKey) then
            moveInput = 1.0
        end
    end

    -- Botón de correr / acelerar (Shift o acción Run)
    local isRunning = false
    if Slip.Input and Slip.Input.GetKey then
        local shiftKey = (Slip.Key and Slip.Key.LeftShift) or "LeftShift"
        isRunning = Slip.Input.GetKey(shiftKey) or (Slip.Input.GetAction and Slip.Input.GetAction("Run"))
    end

    -- Botón de salto (Espacio, W, Flecha Arriba o botón de acción táctil)
    local jumpPressed = false
    local jumpHeld = false

    if Slip.Input and Slip.Input.GetActionDown then
        jumpPressed = Slip.Input.GetActionDown("Jump")
        jumpHeld = Slip.Input.GetAction and Slip.Input.GetAction("Jump")
    end

    if not jumpPressed and Slip.Input and Slip.Input.GetKey then
        local spaceKey = (Slip.Key and Slip.Key.Space) or "Space"
        local wKey = (Slip.Key and Slip.Key.W) or "W"
        local upKey = (Slip.Key and Slip.Key.Up) or "Up"

        jumpHeld = Slip.Input.GetKey(spaceKey) or Slip.Input.GetKey(wKey) or Slip.Input.GetKey(upKey)
        if jumpHeld and not self.wasJumpHeld then
            jumpPressed = true
        end
    end
    self.wasJumpHeld = jumpHeld

    -- ------------------------------------------------------------------------
    -- 2. Detección de Suelo y Timers (Coyote Time & Jump Buffer)
    -- ------------------------------------------------------------------------
    -- Consideramos que está en suelo si la velocidad vertical es casi nula tras colisionar
    if self.isGrounded then
        self.coyoteTimer = self.coyoteTimeMax
    else
        self.coyoteTimer = math.max(0.0, self.coyoteTimer - dt)
    end

    if jumpPressed then
        self.jumpBufferTimer = self.jumpBufferMax
    else
        self.jumpBufferTimer = math.max(0.0, self.jumpBufferTimer - dt)
    end

    -- ------------------------------------------------------------------------
    -- 3. Movimiento Horizontal con Inercia Estilo Mario
    -- ------------------------------------------------------------------------
    local targetSpeed = (isRunning and self.runSpeed or self.walkSpeed) * moveInput
    local accelRate = self.isGrounded and self.acceleration or self.airAcceleration
    local deccelRate = self.friction

    if math.abs(moveInput) > 0.01 then
        -- Comprobación de derrape / patinaje (skid): si se quiere mover en sentido opuesto
        local isSkidding = (moveInput > 0 and vx < -0.5) or (moveInput < 0 and vx > 0.5)
        local currentAccel = isSkidding and (accelRate * 1.8) or accelRate
        vx = vx + (targetSpeed - vx) * math.min(1.0, dt * currentAccel)
    else
        -- Frenado progresivo con inercia
        if math.abs(vx) > 0.01 then
            local sign = vx > 0 and 1.0 or -1.0
            vx = vx - sign * math.min(math.abs(vx), dt * deccelRate)
        else
            vx = 0.0
        end
    end

    -- ------------------------------------------------------------------------
    -- 4. Mecánica de Salto con Altura Variable (Super Mario Bros)
    -- ------------------------------------------------------------------------
    if self.jumpBufferTimer > 0.0 and self.coyoteTimer > 0.0 then
        vy = self.jumpForce
        self.isJumping = true
        self.isGrounded = false
        self.coyoteTimer = 0.0
        self.jumpBufferTimer = 0.0
    end

    -- Salto corto: Si suelta el botón mientras todavía va ascendiendo, cortar velocidad
    if self.isJumping and not jumpHeld and vy > 0.0 then
        vy = vy * self.jumpCutMultiplier
        self.isJumping = false
    end

    if vy <= 0.0 then
        self.isJumping = false
    end

    -- Aplicar velocidad calculada al RigidBody 2D
    self.rb2d:SetLinearVelocity(Vec2(vx, vy))

    -- ------------------------------------------------------------------------
    -- 5. Orientación Visual (Girar hacia la dirección de avance)
    -- ------------------------------------------------------------------------
    if moveInput > 0.01 then
        self.facingRight = true
    elseif moveInput < -0.01 then
        self.facingRight = false
    end

    if self.sprite then
        self.sprite.flipX = not self.facingRight
    elseif self.transform then
        local sx = math.abs(self.transform.scale.x)
        self.transform.scale.x = self.facingRight and sx or -sx
    end

    -- ------------------------------------------------------------------------
    -- 6. Animaciones de Sprite (Opcional si tiene SpriteAnimator)
    -- ------------------------------------------------------------------------
    if self.anim and self.anim.Play then
        if not self.isGrounded then
            if vy > 0.5 then
                self.anim:Play("jump")
            else
                self.anim:Play("fall")
            end
        else
            if math.abs(vx) > 0.5 then
                local isSkidding = (moveInput > 0 and vx < -0.5) or (moveInput < 0 and vx > 0.5)
                if isSkidding then
                    self.anim:Play("skid")
                else
                    self.anim:Play(isRunning and "run" or "walk")
                end
            else
                self.anim:Play("idle")
            end
        end
    end

    -- Resetear estado de suelo para comprobarlo con colisiones en el siguiente ciclo
    self.isGrounded = false
end

-- ----------------------------------------------------------------------------
-- Detección de Suelo mediante Colisiones Físicas
-- ----------------------------------------------------------------------------
function Script:OnCollisionStay(other)
    if self.rb2d then
        local vel = self.rb2d:GetLinearVelocity()
        -- Si la velocidad vertical es casi nula o descendente leve, está apoyado en superficie
        if math.abs(vel.y) < 0.35 then
            self.isGrounded = true
        end
    else
        self.isGrounded = true
    end
end

function Script:OnCollisionEnter(other)
    if self.rb2d then
        local vel = self.rb2d:GetLinearVelocity()
        if math.abs(vel.y) < 0.35 then
            self.isGrounded = true
        end
    else
        self.isGrounded = true
    end
end

function Script:OnCollisionExit(other)
    -- Al dejar la superficie
end

return Script

