local ffi = require('ffi')
local lib = require('ray')

--[[
local ctx = lib.ray_ctx_new(1024)
local timer = lib.ray_timer_new(ctx)
lib.ray_timer_start(timer, 1000, 1000)
--]]

--[[
local idle = lib.ray_idle_new(ctx)
lib.ray_idle_start(idle)
--]]

Sched = { }
Sched.IDGEN = 0
Sched.ALIVE = { }
Sched.QUEUE = lib.ray_ctx_new(1024)
Sched.COROS = { }

function Sched:run()
   while true do
      while #self.COROS > 0 do
         local coro = table.remove(self.COROS, 1)
         coroutine.resume(coro)
      end
      local evt = lib.ray_next(self.QUEUE)
      if evt == nil then
         -- no more pending events
         break
      end
      local oid = lib.ray_get_id(evt.self)
      if oid > 0 then
         local obj = self.ALIVE[oid]
         obj:react(evt)
      else
         error("not found")
      end
      lib.ray_done(evt)
   end
end
function Sched:genid()
   self.IDGEN = self.IDGEN + 1
   return self.IDGEN
end
function Sched:add(obj)
   local oid = self:genid()
   if type(obj) == 'thread' then
      self.COROS[#self.COROS + 1] = obj
   elseif obj.cdata then
      lib.ray_set_id(obj.cdata, oid)
   end
   self.ALIVE[oid] = obj
   return oid
end

Fiber = { }
Fiber.__index = Fiber
function Fiber.new(class, func)
   local self = setmetatable({
      coro = coroutine.create(func)    
   }, class)
   return self
end
function Fiber:ready()
   Sched:add(self)
end
function Fiber:suspend()
   return coroutine.yield()
end
function Fiber:resume()
   return coroutine.resume(self.coro)
end

TCPSocket = { }
TCPSocket.__index = TCPSocket
function TCPSocket.new(class)
   local self = { }
   self.cdata = lib.ray_tcp_new(Sched.QUEUE)
   self.read_queue = { }
   self.write_queue = { }
   self.close_queue = { }
   Sched:add(self)
   return setmetatable(self, class)
end
function TCPSocket.new_from_cdata(class, cdata)
   local self = setmetatable({
      cdata = cdata;
      read_queue = { };
      write_queue = { };
      close_queue = { };
   }, class)
   Sched:add(self)
   return self
end
function TCPSocket:react(evt)
   print("TCPSocket:react - evt:", evt)
   if evt.type == 'RAY_ERROR' then
      lib.ray_close(self.cdata)
   elseif evt.type == 'RAY_READ' then
      local data = evt.data
      if #self.read_queue > 0 then
         local coro = table.remove(self.read_queue, 1)
         coroutine.resume(coro, ffi.string(data))
      else
         lib.ray_read_stop(self.cdata)
      end
   elseif evt.type == 'RAY_WRITE' then
      if #self.write_queue > 0 then
         local coro = table.remove(self.write_queue, 1)
         coroutine.resume(coro)
      end
   elseif evt.type == 'RAY_CLOSE' then
      if #self.close_queue > 0 then
         local coro = table.remove(self.close_queue, 1)
         coroutine.resume(coro)
      end
   end
end
function TCPSocket:read()
   print("TCPSocket:read - ", self.cdata)
   local curr = coroutine.running()
   self.read_queue[#self.read_queue + 1] = curr
   lib.ray_read_start(self.cdata, 1024)
   return coroutine.yield()
end
function TCPSocket:write(data)
   local curr = coroutine.running()
   self.write_queue[#self.write_queue + 1] = curr
   lib.ray_write(self.cdata, data, #data)
   return coroutine.yield()
end
function TCPSocket:close()
   local curr = coroutine.running()
   self.close_queue[#self.close_queue + 1] = curr
   lib.ray_close(self.cdata)
   return coroutine.yield()
end

Actor = { }
Actor.__index = Actor
function Actor.new(class, body)
   local self = setmetatable({

   }, class)
   return self
end

local main = Actor:new(function(self, mesg)
   if mesg == 'PING' then
      self.system:send(mesg.sender, 'PONG')
   elseif mesg == 'EXIT' then
      return
   end
end)

TCPServer = { }
TCPServer.__index = TCPServer
function TCPServer.new(class)
   local self = setmetatable({
      cdata = lib.ray_tcp_new(Sched.QUEUE);
      accept_queue = { },
      close_queue  = { },
   }, class)
   Sched:add(self)
   return self
end
function TCPServer:react(evt)
   if evt.type == 'RAY_ERROR' then
      local mesg = evt.info
   elseif evt.type == 'RAY_CONNECTION' then
      local cdata = lib.ray_tcp_new(Sched.QUEUE)
      lib.ray_accept(self.cdata, cdata)
      local sock = TCPSocket:new_from_cdata(cdata)
      if #self.accept_queue > 0 then
         local coro = table.remove(self.accept_queue, 1)
         coroutine.resume(coro, sock)
      end
   elseif evt.type == 'RAY_CLOSE' then
      if #self.close_queue > 0 then
         local coro = table.remove(self.close_queue, 1)
         coroutine.resume(coro)
      end
   end
end
function TCPServer:bind(host, port)
   return lib.ray_tcp_bind(self.cdata, host, port)   
end
function TCPServer:listen(backlog)
   return lib.ray_listen(self.cdata, backlog)
end
function TCPServer:accept()
   local curr, main = coroutine.running()
   if main then
      local client = lib.ray_tcp_new(Sched.QUEUE)
      lib.ray_accept(self.cdata, client)
   else
      self.accept_queue[#self.accept_queue + 1] = curr
      return coroutine.yield()
   end
end

local main = coroutine.create(function()
   local server = TCPServer:new()
   server:bind('127.0.0.1', 8080)
   server:listen(128)
   while true do
      local sock = server:accept()
      local coro = coroutine.create(function()
         while true do
            local data = sock:read()
            if data then
               sock:write(data)
            else
               sock:close()
               break
            end
         end
      end)
      Sched:add(coro)
   end
end)
Sched:add(main)

Sched:run()

