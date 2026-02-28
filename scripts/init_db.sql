-- Chirp Database Schema Initialization Script
-- This script creates the necessary tables for the Chirp chat system

-- Users table (for Auth service)
CREATE TABLE IF NOT EXISTS users (
  id BIGINT AUTO_INCREMENT PRIMARY KEY,
  user_id VARCHAR(255) NOT NULL UNIQUE,
  username VARCHAR(255) NOT NULL UNIQUE,
  email VARCHAR(255) UNIQUE,
  password_hash VARCHAR(255) NOT NULL,
  created_at BIGINT NOT NULL,
  updated_at BIGINT NOT NULL,
  last_login_at BIGINT,
  is_active BOOLEAN DEFAULT TRUE,
  metadata JSON,
  INDEX idx_user_id (user_id),
  INDEX idx_username (username)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- Sessions table (for Auth service)
CREATE TABLE IF NOT EXISTS sessions (
  id BIGINT AUTO_INCREMENT PRIMARY KEY,
  session_id VARCHAR(255) NOT NULL UNIQUE,
  user_id VARCHAR(255) NOT NULL,
  device_id VARCHAR(255),
  platform VARCHAR(50),
  created_at BIGINT NOT NULL,
  expires_at BIGINT NOT NULL,
  last_activity_at BIGINT NOT NULL,
  is_active BOOLEAN DEFAULT TRUE,
  INDEX idx_session_id (session_id),
  INDEX idx_user_id (user_id),
  INDEX idx_expires (expires_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- Messages table (for Chat service)
CREATE TABLE IF NOT EXISTS messages (
  id BIGINT AUTO_INCREMENT PRIMARY KEY,
  message_id VARCHAR(255) NOT NULL UNIQUE,
  sender_id VARCHAR(255) NOT NULL,
  receiver_id VARCHAR(255),
  channel_id VARCHAR(255) NOT NULL,
  channel_type INT NOT NULL,
  msg_type INT NOT NULL,
  content TEXT,
  timestamp BIGINT NOT NULL,
  created_at BIGINT NOT NULL,
  INDEX idx_channel (channel_id, channel_type, timestamp),
  INDEX idx_receiver (receiver_id, timestamp),
  INDEX idx_timestamp (timestamp),
  INDEX idx_sender (sender_id, timestamp)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- Read receipts table (for Chat service)
CREATE TABLE IF NOT EXISTS read_receipts (
  id BIGINT AUTO_INCREMENT PRIMARY KEY,
  message_id VARCHAR(255) NOT NULL,
  user_id VARCHAR(255) NOT NULL,
  read_at BIGINT NOT NULL,
  INDEX idx_message (message_id),
  INDEX idx_user (user_id),
  UNIQUE KEY unique_message_user (message_id, user_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- Read cursors table (for Chat service - tracks user's last read position)
CREATE TABLE IF NOT EXISTS read_cursors (
  id BIGINT AUTO_INCREMENT PRIMARY KEY,
  user_id VARCHAR(255) NOT NULL,
  channel_id VARCHAR(255) NOT NULL,
  channel_type INT NOT NULL,
  last_read_message_id VARCHAR(255),
  last_read_timestamp BIGINT NOT NULL,
  unread_count INT DEFAULT 0,
  UNIQUE KEY unique_user_channel (user_id, channel_id, channel_type),
  INDEX idx_user (user_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- Groups table (for Chat service)
CREATE TABLE IF NOT EXISTS groups (
  id BIGINT AUTO_INCREMENT PRIMARY KEY,
  group_id VARCHAR(255) NOT NULL UNIQUE,
  group_name VARCHAR(255) NOT NULL,
  description TEXT,
  avatar_url VARCHAR(512),
  owner_id VARCHAR(255) NOT NULL,
  max_members INT DEFAULT 0,
  created_at BIGINT NOT NULL,
  updated_at BIGINT NOT NULL,
  INDEX idx_group_id (group_id),
  INDEX idx_owner (owner_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- Group members table (for Chat service)
CREATE TABLE IF NOT EXISTS group_members (
  id BIGINT AUTO_INCREMENT PRIMARY KEY,
  group_id VARCHAR(255) NOT NULL,
  user_id VARCHAR(255) NOT NULL,
  role INT DEFAULT 0,
  joined_at BIGINT NOT NULL,
  last_read_at BIGINT,
  UNIQUE KEY unique_group_user (group_id, user_id),
  INDEX idx_group_id (group_id),
  INDEX idx_user_id (user_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- Friends table (for Social service)
CREATE TABLE IF NOT EXISTS friends (
  id BIGINT AUTO_INCREMENT PRIMARY KEY,
  user_id VARCHAR(255) NOT NULL,
  friend_user_id VARCHAR(255) NOT NULL,
  status INT DEFAULT 0,
  added_at BIGINT NOT NULL,
  UNIQUE KEY unique_friendship (user_id, friend_user_id),
  INDEX idx_user_id (user_id),
  INDEX idx_friend_user_id (friend_user_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- Friend requests table (for Social service)
CREATE TABLE IF NOT EXISTS friend_requests (
  id BIGINT AUTO_INCREMENT PRIMARY KEY,
  request_id VARCHAR(255) NOT NULL UNIQUE,
  from_user_id VARCHAR(255) NOT NULL,
  to_user_id VARCHAR(255) NOT NULL,
  message TEXT,
  status INT DEFAULT 0,
  created_at BIGINT NOT NULL,
  responded_at BIGINT,
  INDEX idx_from_user (from_user_id),
  INDEX idx_to_user (to_user_id),
  INDEX idx_status (status)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- Blocked users table (for Social service)
CREATE TABLE IF NOT EXISTS blocked_users (
  id BIGINT AUTO_INCREMENT PRIMARY KEY,
  user_id VARCHAR(255) NOT NULL,
  blocked_user_id VARCHAR(255) NOT NULL,
  blocked_at BIGINT NOT NULL,
  UNIQUE KEY unique_block (user_id, blocked_user_id),
  INDEX idx_user_id (user_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- Voice rooms table (for Voice service)
CREATE TABLE IF NOT EXISTS voice_rooms (
  id BIGINT AUTO_INCREMENT PRIMARY KEY,
  room_id VARCHAR(255) NOT NULL UNIQUE,
  room_type INT NOT NULL,
  room_name VARCHAR(255),
  max_participants INT DEFAULT 0,
  created_by VARCHAR(255) NOT NULL,
  created_at BIGINT NOT NULL,
  is_active BOOLEAN DEFAULT TRUE,
  INDEX idx_room_id (room_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- Voice room participants table (for Voice service)
CREATE TABLE IF NOT EXISTS voice_participants (
  id BIGINT AUTO_INCREMENT PRIMARY KEY,
  room_id VARCHAR(255) NOT NULL,
  user_id VARCHAR(255) NOT NULL,
  joined_at BIGINT NOT NULL,
  left_at BIGINT,
  state INT DEFAULT 0,
  UNIQUE KEY unique_room_user (room_id, user_id),
  INDEX idx_room_id (room_id),
  INDEX idx_user_id (user_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
