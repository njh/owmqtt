#!/usr/bin/env ruby

require 'rubygems'
require 'mqtt/client'
require 'json'


raise "Error: PACHUBE_API_KEY is not set" if ENV['PACHUBE_API_KEY'].nil? 

def update_datastream_value(feed, datastream, value)
  result = system(
    'curl', '--silent',
    '--output', '/dev/null',
    '--request', 'PUT',
    '--data-binary', {'current_value' => value}.to_json,
    '--header', 'Accept: application/json',
    '--header', 'Content-Type: application/json',
    '--header', 'X-PachubeApiKey: ' + ENV['PACHUBE_API_KEY'],
    "http://api.pachube.com/v2/feeds/#{feed}/datastreams/#{datastream}"
  )
  puts " * OK" if result
end


client = MQTT::Client.new('localhost')
client.connect do
  client.subscribe('/1wire/#')
  loop do
    topic,message = client.get
    puts "#{topic}: #{message}"
    
    if topic == '/1wire/28537FEE02000078/temperature'
      update_datastream_value(42662, 1, message)
    end
  end
end
