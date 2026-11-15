
-- 	Vector3 helper functions


-- output vector as a string (the KeyValue friendly type)
	function vecString( vec )
		return string.format("%.2f", vec.x) .. " " .. string.format("%.2f", vec.y) .. " " .. string.format("%.2f", vec.z)	
	end
	
-- add two vectors
	function vecAdd( v1, v2 )
		return vector3( v1.x+v2.x, v1.y+v2.y, v1.z+v2.z )	
	end

-- subtract vectors
	function vecSub( v1, v2 )
		return vector3( v1.x-v2.x, v1.y-v2.y, v1.z-v2.z )	
	end
	
-- multiply vectors
	function vecMul( v1, v2 )
	
		if ( tonumber(v2) ) then
		
			return vector3( v1.x*v2, v1.y*v2, v1.z*v2 )	
		
		end
	
		return vector3( v1.x*v2.x, v1.y*v2.y, v1.z*v2.z )	
	end

-- crossproduct vectors
	function vecCrossProduct( v1, v2 )
		return vector3( v1.y*v2.z - v1.z*v2.y, v1.z*v2.x - v1.x*v2.z, v1.x*v2.y - v1.y*v2.x )	
	end

-- dotproduct vectors
	function vecDotProduct( v1, v2 )
		return v1.x*v2.x + v1.y*v2.y + v1.z*v2.z
	end

-- normalize vectors
	function vecNormalize(v1)
		local length = vecLength(v1)

		if (length) then
			local ilength = 1/length
			return vector3(v1.x * ilength, v1.y * ilength, v1.z * ilength)
		end

		return vector3(0, 0, 0)
	end
		

-- find the length of a vector
	function vecLength(v1)
		return math.sqrt(v1.x*v1.x + v1.y*v1.y + v1.z*v1.z);
	end
