-- Movimiento simple con los ejes Horizontal/Vertical y la acción Jump.
-- Sirve con teclado, mando y con el joystick y el botón táctil de Android,
-- porque todos escriben en las mismas acciones y ejes.
--
-- En la entidad: RigidBody (gravedad activa) y un collider.
-- En el inspector del RigidBody, congela la rotación en X y Z para que no se tumbe.
-- Asigna este script a un Script Component.

local Script = {}

Script.moveSpeed = 6.0
Script.jumpSpeed = 7.0

function Script:OnStart()
    self.grounded = false
    self.isJumping = false
    local body = self:GetRigidBody()
    if body and body.SetFriction then
        body:SetFriction(0.0)
    end
end

function Script:OnUpdate(dt)
    local horizontal = Slip.Input.GetAxis("Horizontal")
    local vertical = Slip.Input.GetAxis("Vertical")

    local body = self:GetRigidBody()
    if body then
        local velocity = body:GetLinearVelocity()
        local moveY = velocity.y

        if self.grounded and Slip.Input.GetActionDown("Jump") then
            moveY = self.jumpSpeed
            self.isJumping = true
            self.grounded = false
        end

        if not self.isJumping then
            if moveY > 0.0 then
                moveY = 0.0
            end
        else
            if velocity.y <= 0.0 then
                self.isJumping = false
            end
        end

        body:SetLinearVelocity(Vec3(horizontal * self.moveSpeed, moveY, vertical * self.moveSpeed))
    else
        local transform = self:GetTransform()
        if transform then
            transform:Translate(horizontal * self.moveSpeed * dt, 0, vertical * self.moveSpeed * dt)
        end
    end

    self.grounded = false
end

function Script:OnCollisionStay(other)
    -- El suelo se comprueba mediante raycast hacia abajo para evitar que paredes cuenten como suelo
    local transform = self:GetTransform()
    if transform and Slip.Physics and Slip.Physics.Raycast then
        local pos = transform.position
        local hit = Slip.Physics.Raycast(Vec3(pos.x, pos.y + 0.15, pos.z), Vec3(pos.x, pos.y - 0.25, pos.z), self.entity)
        if hit and hit.hasHit and hit.normal and hit.normal.y > 0.55 then
            self.grounded = true
        end
    end
end

return Script
